
/**
 * @file server_replication_impl.inl
 * @brief log replication for Server<Cfg> (Raft 5.3).
 * Covers the leader->follower replication path and commit/apply tail:
 *   - send_append_entries (falls through to InstallSnapshot when the follower
 *     has fallen behind the snapshot edge see server_snapshot_impl.inl).
 *   - both AppendEntries handlers (request + response), incl. the (5.3)
 *     conflict-term back-up optimization although it is documented this would
 *     rarely be used.
 *   - leader_advance_commit (5.3, 5.4: only commit current-term entries by
 *     count)
 *   - apply_committed (F2 All Servers: advance lastApplied -> state machine)
 * NOTE: INCLUDE server.hpp, not this file.
 */

template <typename Cfg>
void Server<Cfg>::send_append_entries(Peer &peer) noexcept {

  // Build and send an AppendEntries (or InstallSnapshot) to one peer.

  if (peer.is_self) {
    return;
  }

  const Index prev_index = peer.next_index - 1;

  // If the referenced entry has been compacted away, the follower is too far
  // behind -> send a snapshot instead (7).
  if (prev_index < m_log.base()) {
    send_install_snapshot(peer);
    return;
  }

  const std::optional<Term> prev_term = m_log.term_at(prev_index);
  if (!prev_term) {
    send_install_snapshot(peer);
    return;
  }

  // Build the entries to append.
  AppendEntries<Cfg> ae_msg{};
  ae_msg.m_term = m_current_term;
  ae_msg.m_leader_id = m_self_id;
  ae_msg.m_prev_log_index = prev_index;
  ae_msg.m_prev_log_term = *prev_term;
  ae_msg.m_leader_commit = m_commit_index;

  std::uint16_t num_entries = 0;
  for (Index idx = peer.next_index;
       idx <= m_log.last_index() && num_entries < Cfg::MAX_APPEND_ENTRIES;
       ++idx) {
    EntryT const *const entry = m_log.at(idx);
    if (entry == nullptr) {
      break;
    }
    ae_msg.m_entries[num_entries++] = *entry;
  }
  ae_msg.n_entries = num_entries;

  // Send the message to peer.
  MessageT msg{};
  msg.from = m_self_id;
  msg.to = peer.id;
  msg.payload = ae_msg;
  emit(msg);
}

template <typename Cfg>
void Server<Cfg>::handle(const AppendEntries<Cfg> &ae) noexcept {

  // (F2 AppendEntries receiver)
  MessageT reply{};
  reply.from = m_self_id;
  reply.to = ae.m_leader_id;
  AppendEntriesResp resp{};

  // Reply false if term < currentTerm (5.1).
  if (ae.m_term < m_current_term) {
    resp.m_term = m_current_term;
    resp.m_success = false;
    resp.m_last_log_index = m_log.last_index();
    reply.payload = resp;
    emit(reply);
    return;
  }

  // Valid leader for >= our term: adopt term, become follower, reset timer.
  maybe_step_down(ae.m_term);
  become_follower(ae.m_term, ae.m_leader_id);
  m_last_leader_contact = m_cbs.m_now();

  // Reply false if log lacks an entry at prevLogIndex whose term matches
  // prevLogTerm (5.3). Provide a back-up hint.
  if (ae.m_prev_log_index > m_log.last_index()) {
    resp.m_term = m_current_term;
    resp.m_success = false;
    resp.m_conflict_index = m_log.last_index() + 1;
    resp.m_conflict_term = 0;
    resp.m_last_log_index = m_log.last_index();
    reply.payload = resp;
    emit(reply);
    return;
  }

  // prev_log_index may sit at/below our snapshot edge.
  if (ae.m_prev_log_index < m_log.base()) {
    // Everything up to log_base is already known-committed via snapshot;
    // accept and let the matching below skip already-present entries.
  } else if (const std::optional<Term> prev_term =
                 m_log.term_at(ae.m_prev_log_index);
             prev_term.has_value()) {
    if (*prev_term != ae.m_prev_log_term) {
      // Conflict: report the first index of the conflicting term so the
      // leader can skip the whole term in one step (5.3 optimisation).
      Index current_idx = ae.m_prev_log_index;
      for (Index scan = ae.m_prev_log_index; scan > m_log.base() + 1; --scan) {
        const std::optional<Term> term = m_log.term_at(scan - 1);
        if (!term.has_value() || term.value() != *prev_term) {
          break;
        }
        current_idx = scan - 1;
      }
      resp.m_term = m_current_term;
      resp.m_success = false;
      resp.m_conflict_term = *prev_term;
      resp.m_conflict_index = current_idx;
      resp.m_last_log_index = m_log.last_index();
      reply.payload = resp;
      emit(reply);
      return;
    }
  } else {
    // prev term unknown (shouldn't happen given checks) -> need snapshot.
    resp.m_term = m_current_term;
    resp.m_success = false;
    resp.m_last_log_index = m_log.last_index();
    reply.payload = resp;
    emit(reply);
    return;
  }

  // For each new entry: if an existing entry conflicts (same index, different
  // term) delete it and all that follow, then append new entries not already
  // present (5.3).
  const auto entries = ae.view();
  for (std::uint16_t i = 0; i < entries.size(); ++i) {
    const EntryT &next_entry = entries[i];
    const Index next_idx = ae.m_prev_log_index + 1 + i;

    if (next_idx <= m_log.base()) {
      // already in snapshot
      continue;
    }
    if (next_idx <= m_log.last_index()) {
      if (const std::optional<Term> existing = m_log.term_at(next_idx)) {
        if (*existing == next_entry.m_term) {
          // already present, identical
          continue;
        }
        // conflict: truncate from next_idx onward
        m_log.truncate_suffix(next_idx);
        detail::call_if(m_cbs.m_persist_log_truncate, next_idx);
      }
    }
    // append
    EntryT entry = next_entry;
    entry.m_index = next_idx;
    if (ok(m_log.push(entry))) {
      detail::call_if(m_cbs.m_persist_log_append, entry);
    }
  }

  // If leaderCommit > commitIndex, set commitIndex = min(leaderCommit, index of
  // last new entry).
  if (ae.m_leader_commit > m_commit_index) {
    const Index last_new = ae.m_prev_log_index + ae.n_entries;
    const Index newc = std::min(ae.m_leader_commit, last_new);
    m_commit_index = std::max(newc, m_commit_index);
  }

  resp.m_term = m_current_term;
  resp.m_success = true;
  resp.m_last_log_index = m_log.last_index();
  reply.payload = resp;
  emit(reply);
  apply_committed();
}

template <typename Cfg> void Server<Cfg>::leader_advance_commit() noexcept {
  // (F2 Leaders) advance commitIndex: highest N replicated on a majority whose
  // entry is from the current term (5.3, 5.4).

  const Index last = m_log.last_index();
  for (Index next = last; next > m_commit_index; --next) {

    const auto term = m_log.term_at(next);
    if (!term.has_value()) {
      continue;
    }

    if (term.value() != m_current_term) {
      // never commit prior terms by count
      continue;
    }

    std::size_t count = 0;
    for (std::size_t i = 0; i < m_n_nodes; ++i) {
      if (m_nodes[i].is_self || m_nodes[i].match_index >= next) {
        ++count;
      }
    }

    if (count >= majority()) {
      m_commit_index = next;
      break;
    }
  }
  apply_committed();
}

template <typename Cfg>
void Server<Cfg>::handle(NodeId from, const AppendEntriesResp &rr) noexcept {

  if (rr.m_term > m_current_term) {
    maybe_step_down(rr.m_term);
    return;
  }

  if (m_state != State::LEADER || rr.m_term < m_current_term) {
    return;
  }

  Peer *const peer = node_by_id(from);
  if (peer == nullptr) {
    return;
  }
  peer->last_contact = m_cbs.m_now();

  if (rr.m_success) {
    // update nextIndex / matchIndex (5.3)
    peer->next_index = std::max(rr.m_last_log_index + 1, peer->next_index);
    peer->match_index = std::max(rr.m_last_log_index, peer->match_index);
    leader_advance_commit();
    // If more entries remain, pipeline another batch immediately.
    if (peer->next_index <= m_log.last_index()) {
      send_append_entries(*peer);
    }
  } else {
    // Failed consistency check: back nextIndex up. Use the conflict hint to
    // skip a whole term where possible (5.3 optimization).
    Index backup;
    if (rr.m_conflict_term != 0) {
      Index found = 0;
      for (Index idx = m_log.last_index(); idx > m_log.base(); --idx) {
        const std::optional<Term> term = m_log.term_at(idx);
        if (term && *term == rr.m_conflict_term) {
          found = idx;
          break;
        }
      }
      backup = (found != 0) ? (found + 1) : rr.m_conflict_index;
    } else {
      backup = rr.m_conflict_index;
      if (backup == 0) {
        backup = (peer->next_index > 1) ? peer->next_index - 1 : 1;
      }
    }
    backup = std::max<Index>(backup, 1);
    peer->next_index = backup;
    // retry (F2)
    send_append_entries(*peer);
    apply_committed();
  }
}

template <typename Cfg> void Server<Cfg>::apply_committed() noexcept {
  // (F2 All Servers) while commitIndex > lastApplied: apply next entry.
  while (m_commit_index > m_last_applied) {
    ++m_last_applied;
    const EntryT *entry = m_log.at(m_last_applied);
    if (entry == nullptr) {
      // compacted (already applied before snapshot)
      continue;
    }
    detail::call_if(m_cbs.m_apply, *entry);
  }

  maybe_compact();
}

template <typename Cfg> void Server<Cfg>::maybe_compact() noexcept {
  const Index live = m_last_applied - m_log.base();
  if (live < Cfg::SNAPSHOT_THRESHOLD) {
    return;
  }
  (void)begin_snapshot();
}
