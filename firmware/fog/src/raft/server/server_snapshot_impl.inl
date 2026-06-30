/**
 * @file server_snapshot_impl.inl
 * @brief Covers Raft Section 7 / Figure 13:
 *   - send_install_snapshot.
 *   - both InstallSnapshot handlers (follower reassembly + leader response).
 *   - begin_snapshot.
 * NOTE: INCLUDE server.hpp, not this file.
 */

template <typename Cfg>
void Server<Cfg>::send_install_snapshot(const Peer &peer) noexcept {
  // This build keeps the whole snapshot in m_snap_buf and sends it in one
  // message.
  if (peer.is_self) {
    return;
  }

  if (m_snap_len == 0) {
    return; // no snapshot taken yet
  }

  InstallSnapshot<Cfg> is{};
  is.m_term = m_current_term;
  is.m_leader_id = m_self_id;
  is.m_last_included_index = m_log.base();
  is.m_last_included_term = m_log.base_term();
  is.m_offset = 0;
  is.m_total_len = m_snap_len;
  is.m_done = true;
  is.m_data_len = static_cast<std::uint16_t>(
      std::min<std::uint32_t>(m_snap_len, Cfg::SNAPSHOT_CHUNK));
  std::memcpy(is.m_data.data(), m_snap_buf.data(), is.m_data_len);

  MessageT msg{};
  msg.from = m_self_id;
  msg.to = peer.id;
  msg.payload = is;
  emit(msg);
  apply_committed();
}

template <typename Cfg>
void Server<Cfg>::handle(const InstallSnapshot<Cfg> &is) noexcept {
  // (Figure 13 receiver)

  MessageT reply{};
  reply.from = m_self_id;
  reply.to = is.m_leader_id;

  // Reply immediately if term < currentTerm.
  if (is.m_term < m_current_term) {
    reply.payload = InstallSnapshotResp{m_current_term, m_log.base()};
    emit(reply);
    return;
  }

  maybe_step_down(is.m_term);
  become_follower(is.m_term, is.m_leader_id);

  // Accumulate chunks.
  if (is.m_offset == 0) {
    m_snap_len = 0;
    m_snap_index = is.m_last_included_index;
    m_snap_term = is.m_last_included_term;
  }

  if (is.m_offset + is.m_data_len <= Cfg::SNAPSHOT_MAX) {
    std::memcpy(m_snap_buf.data() + is.m_offset, is.m_data.data(),
                is.m_data_len);
    if (is.m_offset + is.m_data_len > m_snap_len) {
      m_snap_len = is.m_offset + is.m_data_len;
    }
  }

  if (!is.m_done) {
    reply.payload = InstallSnapshotResp{m_current_term, m_log.base()};
    emit(reply);
    return;
  }

  // If an existing log entry matches the snapshot's last included entry, retain
  // following entries; otherwise discard the whole log.
  const std::optional<Term> term = m_log.term_at(is.m_last_included_index);
  const bool retain = (is.m_last_included_index <= m_log.last_index()) &&
                      term && (*term == is.m_last_included_term);
  if (retain) {
    m_log.compact_to(is.m_last_included_index, is.m_last_included_term);
  } else {
    m_log.reset_to_snapshot(is.m_last_included_index, is.m_last_included_term);
  }

  m_commit_index = std::max(m_commit_index, m_log.base());
  m_last_applied = std::max(m_last_applied, m_log.base());

  // Reset state machine using the snapshot contents.
  detail::call_if(m_cbs.m_snapshot_load,
                  reinterpret_cast<const std::uint8_t *>(m_snap_buf.data()),
                  m_snap_len);
  detail::call_if(m_cbs.m_persist_log_compact, m_log.base());

  reply.payload = InstallSnapshotResp{m_current_term, m_log.base()};
  emit(reply);
  apply_committed();
}

template <typename Cfg>
void Server<Cfg>::handle(NodeId from, const InstallSnapshotResp &rr) noexcept {
  if (rr.m_term > m_current_term) {
    maybe_step_down(rr.m_term);
    return;
  }

  if (m_state != State::LEADER) {
    return;
  }

  Peer *const peer = node_by_id(from);
  if (peer == nullptr) {
    return;
  }
  peer->last_contact = m_cbs.m_now();
  peer->next_index = std::max(rr.m_last_included_index + 1, peer->next_index);
  peer->match_index = std::max(rr.m_last_included_index, peer->match_index);

  // continue normal replication
  send_append_entries(*peer);
  apply_committed();
}

template <typename Cfg> Error Server<Cfg>::begin_snapshot() noexcept {
  if (!detail::bound(m_cbs.m_snapshot_save)) {
    return Error::BAD_ARG;
  }

  if (m_last_applied <= m_log.base()) {
    // Nothing to compact
    return Error::OK;
  }

  const Index snap_index = m_last_applied;
  const std::optional<Term> snap_term = m_log.term_at(snap_index);
  if (!snap_term) {
    return Error::NOT_FOUND;
  }

  const int len = m_cbs.m_snapshot_save(
      reinterpret_cast<std::uint8_t *>(m_snap_buf.data()), Cfg::SNAPSHOT_MAX);
  if (len < 0) {
    return Error::LOG_FULL;
  }
  m_snap_len = static_cast<std::uint32_t>(len);

  // Drop entries (base, snap_index] from the ring, adopting snap_index as the
  // new snapshot edge.
  m_log.compact_to(snap_index, *snap_term);

  detail::call_if(m_cbs.m_persist_log_compact, snap_index);
  return Error::OK;
}
