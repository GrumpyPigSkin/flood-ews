

/**
 * @file server_election_impl.inl
 * @brief leader election + role transitions for Server<Cfg>.
 * Covers Raft (5.2) leader election and the (5.4) up-to-date voting
 * restriction:
 *   - set_state / reset_election_timer / maybe_step_down / become_follower
 *   - become_candidate / become_leader
 *   - send_request_vote and both RequestVote handlers (request + response)
 * NOTE: INCLUDE server.hpp, not this file.
 */

template <typename Cfg>
void Server<Cfg>::set_state(const State state) noexcept {
  const State old = m_state;
  if (old == state) {
    return;
  }
  m_state = state;
  detail::call_if(m_cbs.m_on_state_change, old, state);
}

template <typename Cfg> void Server<Cfg>::reset_election_timer() noexcept {
  std::uint32_t jitter = 0;
  const Time now = m_cbs.m_now();

  if constexpr (Cfg::ELECTION_TIMEOUT_SPREAD_MS > 0) {
    jitter = m_cbs.m_rand() % (Cfg::ELECTION_TIMEOUT_SPREAD_MS + 1);
  }

  m_election_deadline = now + Cfg::ELECTION_TIMEOUT_MIN_MS + jitter;
}

template <typename Cfg>
void Server<Cfg>::maybe_step_down(const Term term) noexcept {
  // (F2, 5.1) If we see a higher term, adopt it and step down.
  if (term > m_current_term) {
    m_current_term = term;
    m_voted_for = BAD_NODE;
    persist_state();
    if (m_state != State::FOLLOWER) {
      set_state(State::FOLLOWER);
      m_current_leader = BAD_NODE;
    }
  }
}

template <typename Cfg>
void Server<Cfg>::become_follower(const Term term,
                                  const NodeId leader) noexcept {
#ifdef ENABLE_FAULT_INJECTION
  logging::inf("RAFT:BECAME_FOLLOWER");
#endif
  // Reset election state.
  if (term > m_current_term) {
    m_current_term = term;
    m_voted_for = BAD_NODE;
    persist_state();
  }

  // Become follower and reset timer
  set_state(State::FOLLOWER);
  m_current_leader = leader;
  if (leader != BAD_NODE) {
    m_leader_hint = leader;
  }

  reset_election_timer();
}

template <typename Cfg>
void Server<Cfg>::send_request_vote(const Peer &peer) noexcept {
  MessageT msg{};
  msg.from = m_self_id;
  msg.to = peer.id;
  msg.payload = RequestVote{
      .m_term = m_current_term,
      .m_candidate_id = m_self_id,
      .m_last_log_index = m_log.last_index(),
      .m_last_log_term = m_log.last_term(),
  };
  emit(msg);
}

template <typename Cfg> void Server<Cfg>::become_candidate() noexcept {
  // (F2 Candidates) On conversion to candidate, start an election.
#ifdef ENABLE_FAULT_INJECTION
  logging::inf("RAFT:BECAME_CANDIDATE");
#endif
  set_state(State::CANDIDATE);
  m_current_term += 1;     // increment currentTerm
  m_voted_for = m_self_id; // vote for self
  persist_state();
  m_current_leader = BAD_NODE;
  m_votes_granted = 0;

  for (std::size_t i = 0; i < m_n_nodes; ++i) {
    m_nodes[i].vote_granted = false;
  }

  if (Peer *s = self()) {
    s->vote_granted = true;
    m_votes_granted = 1;
  }

  reset_election_timer();

  for (std::size_t i = 0; i < m_n_nodes; ++i) {
    if (!m_nodes[i].is_self) {
      send_request_vote(m_nodes[i]);
    }
  }

  // Degenerate single-node cluster: our own vote is already a majority.
  if (m_votes_granted >= majority()) {
    become_leader();
  }
}

template <typename Cfg> void Server<Cfg>::become_leader() noexcept {

  // (F2 Leaders) On election: init nextIndex/matchIndex, send heartbeats, and
  // commit a no-op for this term so reads/commit can advance (5.4.2, 8).

  const Index last = m_log.last_index();

#ifdef ENABLE_FAULT_INJECTION
  logging::inf("RAFT:BECAME_LEADER");
#endif
  set_state(State::LEADER);
  m_current_leader = m_self_id;
  m_leader_hint = m_self_id;

  for (std::size_t i = 0; i < m_n_nodes; ++i) {
    m_nodes[i].next_index = last + 1; // init to leader last index + 1
    m_nodes[i].match_index = 0;
  }

  if (Peer *s = self()) {
    s->match_index = last;
  }

  // No-op entry to establish commitment in the current term (8).
  EntryT entry{};
  entry.m_term = m_current_term;
  entry.m_index = last + 1;
  entry.m_type = EntryType::NOOP;
  entry.m_data_len = 0;

  if (ok(m_log.push(entry))) {
    detail::call_if(m_cbs.m_persist_log_append, entry);
    if (Peer *s = self()) {
      s->match_index = entry.m_index;
    }
  }

  const Time now = m_cbs.m_now();
  for (std::size_t i = 0; i < m_n_nodes; ++i) {
    m_nodes[i].last_contact = now;
  }
  m_leader_since = now;

  // force an immediate heartbeat
  m_last_heartbeat_sent = 0;
}

template <typename Cfg>
bool Server<Cfg>::request_vote_guard(const RequestVote &rv) noexcept {

  // Handle a pre-vote request.
  const Time now = m_cbs.m_now();
  if (rv.m_pre_vote) {
    // Would we vote in a real election?
    bool would_grant = false;
    if (!leader_is_live(now) && rv.m_term >= m_current_term) {
      const Index my_last_idx = m_log.last_index();
      const Term my_last_trm = m_log.last_term();
      would_grant = (rv.m_last_log_term > my_last_trm) ||
                    (rv.m_last_log_term == my_last_trm &&
                     rv.m_last_log_index >= my_last_idx);
    }
    MessageT reply{};
    reply.from = m_self_id;
    reply.to = rv.m_candidate_id;
    reply.payload = RequestVoteResp{m_current_term, would_grant, true};
    emit(reply);
    return true;
  }

  // Guard against a disruptive candidate. If we currently believe we have a
  // live leader we should reject incoming calls from nodes with higher terms
  // experiencing churn. This could happen when the disruptive node is able to
  // send but is unable to receive.
  if (leader_is_live(now) && rv.m_term > m_current_term) {
    // Do not step down and do not adopt the term. Reply with our own term so
    // the disruptive candidate learns nothing that helps it.
    MessageT reply{};
    reply.from = m_self_id;
    reply.to = rv.m_candidate_id;
    reply.payload = RequestVoteResp{m_current_term, false};
    emit(reply);
    return true;
  }

  return false;
}

template <typename Cfg>
void Server<Cfg>::handle(const RequestVote &rv) noexcept {

  // Guard against a disruptive leaders.
  // Emits a response.
  if (request_vote_guard(rv)) {
    return;
  }

  maybe_step_down(rv.m_term);

  MessageT reply{};
  reply.from = m_self_id;
  // reply to the CANDIDATE (5.2)
  reply.to = rv.m_candidate_id;
  bool grant = false;

  // Reply false if term < currentTerm (5.1).
  if (rv.m_term < m_current_term) {
    reply.payload = RequestVoteResp{m_current_term, false};
    emit(reply);
    return;
  }

  // If votedFor is null or candidateId, and the candidate's log is at least
  // as up-to-date as ours, grant the vote (5.2, 5.4).
  if (m_voted_for == BAD_NODE || m_voted_for == rv.m_candidate_id) {
    const Index my_last_idx = m_log.last_index();
    const Term my_last_trm = m_log.last_term();
    const bool up_to_date = (rv.m_last_log_term > my_last_trm) ||
                            (rv.m_last_log_term == my_last_trm &&
                             rv.m_last_log_index >= my_last_idx);
    if (up_to_date) {
      grant = true;
      m_voted_for = rv.m_candidate_id;
      persist_state();
      // granting a vote resets our timer
      reset_election_timer();
    }
  }

  reply.payload = RequestVoteResp{m_current_term, grant};
  emit(reply);
}

template <typename Cfg>
void Server<Cfg>::handle(NodeId from, const RequestVoteResp &rr) noexcept {

  if (rr.m_pre_vote) {
    if (!m_pre_vote_active) {
      return;
    }
    // A real leader out there will answer our probe with a higher term. At
    // this point we only count grants.
    if (rr.m_vote_granted && rr.m_term <= m_current_term + 1) {
      ++m_pre_votes_granted;
      if (m_pre_votes_granted >= majority()) {
        m_pre_vote_active = false;
        // Now increment term and run the real election
        become_candidate();
      }
    }
    return;
  }

  if (m_state != State::CANDIDATE) {
    return;
  }

  // Their term is greater than ours, step down.
  if (rr.m_term > m_current_term) {
    maybe_step_down(rr.m_term);
    return;
  }

  // If their term is less than our term, they are replying to old data,
  // ignore.
  if (rr.m_term < m_current_term) {
    return;
  }

  Peer *const peer = node_by_id(from);
  if ((peer == nullptr) || peer->vote_granted) {
    // unknown or already counted
    return;
  }

  if (rr.m_vote_granted) {
    peer->vote_granted = true;
    ++m_votes_granted;
    // If we get a majority of votes become leader.
    if (m_votes_granted >= majority()) {
      become_leader();
    }
  }
}
