/**
 * @file server_driver_impl.inl
 * public driver + client API for Server<Cfg>.
 * Covers the externally-driven entry points:
 *   - periodic   (F2 timing: heartbeats when leader, election timeout
 *     otherwise)
 *   - submit     (leader-only client append + immediate replication kick)
 *   - can_read   (8: read-only linearisability precondition)
 */

template <typename Cfg>
Server<Cfg>::Server(NodeId self_id, std::span<const NodeId> node_ids,
                    const CallbacksT &cbs) noexcept
    : m_cbs{cbs}, m_self_id{self_id} {
  if (node_ids.empty() || node_ids.size() > Cfg::MAX_NODES || !m_cbs.valid()) {
    m_valid = false;
    return;
  }

  bool found_self = false;
  m_n_nodes = node_ids.size();

  for (std::size_t i = 0; i < m_n_nodes; ++i) {
    m_nodes[i] = Peer{};
    m_nodes[i].id = node_ids[i];
    m_nodes[i].is_self = (node_ids[i] == self_id);
    found_self = found_self || m_nodes[i].is_self;
  }

  m_valid = found_self;
}

template <typename Cfg> void Server<Cfg>::periodic() noexcept {
  if (!m_running) {
    return;
  }
  const Time now = m_cbs.m_now();

  if (m_state == State::LEADER) {

    // Handle when we haven't heard from followers in a while. On the event that
    // we are a leader and we do not hear from followers we need to step down in
    // case they can hear out heartbeats but we cannot hear them, causing a
    // lock.
    if (!contact_with_majority(now)) {
      become_follower(m_current_term, BAD_NODE);
      return;
    }

    // Send heartbeats / pending entries on the heartbeat interval.
    if (now - m_last_heartbeat_sent >= Cfg::HEARTBEAT_INTERVAL_MS) {
      m_last_heartbeat_sent = now;
      for (std::size_t i = 0; i < m_n_nodes; ++i) {
        if (!m_nodes[i].is_self) {
          send_append_entries(m_nodes[i]);
        }
      }
    }
  } else {
    // Follower/candidate: election timeout -> start (new) election (5.2).
    if (static_cast<std::int64_t>(now - m_election_deadline) >= 0) {
      start_pre_vote();
    }
  }
  apply_committed();
}

template <typename Cfg>
bool Server<Cfg>::contact_with_majority(const Time now) const noexcept {
  // The wait needs to be at least as long as the longest election timeout.
  const Time window =
      Cfg::ELECTION_TIMEOUT_MIN_MS + Cfg::ELECTION_TIMEOUT_SPREAD_MS;

  // If we become leader we don't want to judge others before there has been at
  // least the longest timeout.
  if (static_cast<std::int64_t>(now - m_leader_since) <
      static_cast<std::int64_t>(window)) {
    return true;
  }

  // Count self.
  std::size_t reachable = 1;
  for (std::size_t i = 0; i < m_n_nodes; ++i) {
    if (m_nodes[i].is_self) {
      continue;
    }
    if (static_cast<std::int64_t>(now - m_nodes[i].last_contact) <
        static_cast<std::int64_t>(window)) {
      ++reachable;
    }
  }
  return reachable > (m_n_nodes / 2);
}

template <typename Cfg> void Server<Cfg>::start_pre_vote() noexcept {
  m_pre_vote_active = true;
  // we would vote for ourselves
  m_pre_votes_granted = 1;
  reset_election_timer();

  for (std::size_t i = 0; i < m_n_nodes; ++i) {
    if (m_nodes[i].is_self) {
      continue;
    }
    MessageT msg{};
    msg.from = m_self_id;
    msg.to = m_nodes[i].id;
    msg.payload = RequestVote{
        // Probe with term + 1: "if I ran at the next term, would you vote?"
        .m_term = m_current_term + 1,
        .m_candidate_id = m_self_id,
        .m_last_log_index = m_log.last_index(),
        .m_last_log_term = m_log.last_term(),
        .m_pre_vote = true,
    };
    emit(msg);
  }

  // Single-node cluster: our own pre-vote is already a majority.
  if (m_pre_votes_granted >= majority()) {
    m_pre_vote_active = false;
    become_candidate();
  }
}

template <typename Cfg>
Result<Index>
Server<Cfg>::submit(const EntryType type,
                    const std::span<const std::byte> data) noexcept {

  if (!m_running) {
    return std::unexpected(Error::SHUTDOWN);
  }

  if (m_state != State::LEADER) {
    return std::unexpected(Error::NOT_LEADER);
  }

  if (data.size() > Cfg::MAX_ENTRY_DATA) {
    return std::unexpected(Error::BAD_ARG);
  }

  if (m_log.full()) {
    return std::unexpected(Error::LOG_FULL);
  }

  EntryT entry{
      .m_term = m_current_term,
      .m_index = m_log.last_index() + 1,
      .m_type = type,
      .m_data_len = static_cast<std::uint16_t>(data.size()),
  };

  if (!data.empty()) {
    std::memcpy(entry.m_data.data(), data.data(), data.size());
  }

  if (!ok(m_log.push(entry))) {
    return std::unexpected(Error::LOG_FULL);
  }

  detail::call_if(m_cbs.m_persist_log_append, entry);

  if (Peer *s = self()) {
    s->match_index = entry.m_index;
  }

  const Index assigned = entry.m_index;

  // Kick replication immediately rather than waiting for the heartbeat.
  for (std::size_t i = 0; i < m_n_nodes; ++i) {
    if (!m_nodes[i].is_self) {
      send_append_entries(m_nodes[i]);
    }
  }

  // Only a single node cluster submits to itself.
  if (m_n_nodes == 1) {
    leader_advance_commit();
    apply_committed();
  }

  return assigned;
}

template <typename Cfg> bool Server<Cfg>::can_read() const noexcept {
  // Safe iff leader and we have committed an entry from our current term (the
  // no-op committed on election guarantees this once replicated).
  if (m_state != State::LEADER) {
    return false;
  }

  if (m_commit_index == 0) {
    return false;
  }
  const std::optional<Term> current_term = m_log.term_at(m_commit_index);
  return current_term && *current_term == m_current_term;
}
