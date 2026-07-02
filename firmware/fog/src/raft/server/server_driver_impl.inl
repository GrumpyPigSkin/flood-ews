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
      become_candidate();
    }
  }
  apply_committed();
}

template <typename Cfg>
Result<Index>
Server<Cfg>::submit(const EntryType type,
                    const std::span<const std::byte> data) noexcept {

  if (!m_running) {
    return tl::unexpected(Error::SHUTDOWN);
  }

  if (m_state != State::LEADER) {
    return tl::unexpected(Error::NOT_LEADER);
  }

  if (data.size() > Cfg::MAX_ENTRY_DATA) {
    return tl::unexpected(Error::BAD_ARG);
  }

  if (m_log.full()) {
    return tl::unexpected(Error::LOG_FULL);
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
    return tl::unexpected(Error::LOG_FULL);
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
