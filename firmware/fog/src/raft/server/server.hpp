#pragma once

#include "common/overload.hpp"
#include "raft/callback.hpp"
#include "raft/log/log_ring.hpp"
#include "raft/raft_types.hpp"
#include <array>
#include <cstring>
#include <span>

namespace fog::raft {

template <typename Cfg = DefaultConfig> class Server {
public:
  using EntryT = Entry<Cfg>;
  using MessageT = Message<Cfg>;
  using CallbacksT = Callbacks<Cfg>;

  /**
   * @brief Constructor.
   * @param [in] self_id this nodes ID
   * @param [in] node_ids lists every node in the cluster `self_id` must appear
   * in it.
   * @param [in] cbs the callback hooks, these must be in a valid state.
   */
  Server(NodeId self_id, std::span<const NodeId> node_ids,
         const CallbacksT &cbs) noexcept;

  /** @brief None-copyable but is moveable. */
  Server(const Server &) = delete;
  Server(Server &&) = default;
  Server &operator=(const Server &) = delete;
  Server &operator=(Server &&) = default;
  ~Server() = default;

  /**
   * @brief Returns true if the server found itself in the nodes given in the
   * constructor.
   */
  [[nodiscard]] bool valid() const noexcept { return m_valid; }

  /**
   * @brief Are we the leader.
   * @return true if we are.
   */
  [[nodiscard]] bool is_leader() const noexcept {
    return m_current_leader == m_self_id;
  }

  /**
   * @brief Start the raft server.
   */
  void start() noexcept {
    m_running = true;
    set_state(State::FOLLOWER);
    reset_election_timer();
  }

  /**
   * @brief Stop the server.
   */
  void stop() noexcept { m_running = false; }

  /**
   * @brief Drives time forwards, should be called more frequently than
   * heartbeat ms.
   */
  void periodic() noexcept;

  /**
   * @brief Reading in a whole message and dispatch it to the relevant handle.
   * @param [in] msg The RPC message
   */
  void handle(const MessageT &msg) noexcept {
    auto overloads = tl::overload(
        [&](const RequestVote &rvm) { handle(rvm); },
        [&](const RequestVoteResp &rvrm) { handle(msg.from, rvrm); },
        [&](const AppendEntries<Cfg> &aem) { handle(aem); },
        [&](const AppendEntriesResp &aerm) { handle(msg.from, aerm); },
        [&](const InstallSnapshot<Cfg> &ism) { handle(ism); },
        [&](const InstallSnapshotResp &isrm) { handle(msg.from, isrm); });
    std::visit(overloads, msg.payload);
  }

  /**
   * @brief Leader only, append a client command for replication. On success the
   * result holds the assigned log index.
   * @param [in] EntryType the type of message we want to submit.
   * @param [in] data The data to submit.
   * @return Result<Index>
   */
  [[nodiscard]] Result<Index> submit(EntryType type,
                                     std::span<const std::byte> data) noexcept;

  /**
   * @brief Compact in-memory and persisted log up to last_applied by
   * snapshotting.
   * @return Error
   */
  [[nodiscard]] Error begin_snapshot() noexcept;

  /**
   * @brief Returns true if it is currently safe for this leader to serve a read
   * without going through the log.
   */
  [[nodiscard]] bool can_read() const noexcept;

  /** @brief an entry submitted at `index`. */
  [[nodiscard]] EntryT const *get_entry(const Index index) {
    return m_log.at(index);
  }

  /**
   * @brief Apply the loaded `term` and `voted_for` BEFORE raft is started.
   * @param [in] term
   * @param [in] voted_for
   */
  void restore_state(Term term, NodeId voted_for) noexcept {
    m_current_term = term;
    m_voted_for = voted_for;
  }

private:
  /**
   * @brief Handle a request vote message.
   * @param [in] rv The message.
   */
  void handle(const RequestVote &rv) noexcept;

  /**
   * @brief Handle request vote response message.
   * @param [in] from eui of the node who sent the response.
   * @param [in] rr The message.
   */
  void handle(NodeId from, const RequestVoteResp &rr) noexcept;

  /**
   * @brief Handle an append entries message.
   * @param [in] ae The message.
   */
  void handle(const AppendEntries<Cfg> &ae) noexcept;

  /**
   * @brief Handle an append entries response message.
   * @param [in] from eui of the node who sent the response.
   * @param [in] rr The message.
   */
  void handle(NodeId from, const AppendEntriesResp &rr) noexcept;

  /**
   * @brief Handle an append entries message.
   * @param [in] is The message.
   */
  void handle(const InstallSnapshot<Cfg> &is) noexcept;

  /**
   * @brief Handle a install entries response message.
   * @param [in] from eui of the node who sent the response.
   * @param [in] rr The message.
   */
  void handle(NodeId from, const InstallSnapshotResp &rr) noexcept;

  /**
   * @brief Get the number of nodes needed for a majority.
   * @return std::size_t
   */
  [[nodiscard]] std::size_t majority() const noexcept {
    return (m_n_nodes / 2) + 1;
  }

  /**
   * @brief Get a Peer by it's node ID.
   * @param [in] nid The node to look for.
   * @return Peer* The peer or nullptr if not found.
   */
  [[nodiscard]] Peer *node_by_id(const NodeId nid) noexcept {

    for (std::size_t i = 0; i < m_n_nodes; ++i) {
      if (m_nodes[i].id == nid) {
        return &m_nodes[i];
      }
    }

    return nullptr;
  }

  /**
   * @brief Get the the Peer instance for this node.
   * @return Peer*
   */
  [[nodiscard]] Peer *self() noexcept { return node_by_id(m_self_id); }

  /**
   * @brief Get the the Peer instance for this node.
   * @return Peer*
   */
  [[nodiscard]] Peer *self() const noexcept { return node_by_id(m_self_id); }

  /**
   * @brief Persist the state to none volatile storage.
   */
  void persist_state() const noexcept {
    detail::call_if(m_cbs.m_persist_state, m_current_term, m_voted_for);
  }

  /**
   * @brief Set the state.
   * @param [in] state the new state.
   */
  void set_state(State state) noexcept;

  /**
   * @brief Reset election timeout, called where there is a heartbeat or other
   * message from the leader.
   */
  void reset_election_timer() noexcept;

  /**
   * @brief If we see a higher term, adopt it and step down to follower.
   * @param [in] term The term to check.
   */
  void maybe_step_down(Term term) noexcept;

  /**
   * @brief Set the state to follower, persist the state.
   * @param [in] term The new term to adopt.
   * @param [in] leader Our new leader.
   */
  void become_follower(Term term, NodeId leader) noexcept;

  /**
   * @brief Move from state the current state to become a follower.
   */
  void become_candidate() noexcept;

  /**
   * @brief Move from the current state to become a leader.
   */
  void become_leader() noexcept;

  /**
   * @brief Send a vote request to the Peer peer.
   * @param [in] peer The peer to ask for a vote.
   */
  void send_request_vote(const Peer &peer) noexcept;

  /**
   * @brief Send a AppendEntries to the Peer peer.
   * @param [in] peer The peer to append entries to.
   */
  void send_append_entries(Peer &peer) noexcept;

  /**
   * @brief Send and InstallSnapshot to Peer peer.
   * @param [in] peer The peer to the snapshot to.
   */
  void send_install_snapshot(const Peer &peer) noexcept;

  /**
   * @brief Advance commit index. Highest N replicated on a majority whose entry
   * is from the current term.
   */
  void leader_advance_commit() noexcept;

  /**
   * @brief Call apply for the newly commited entries.
   */
  void apply_committed() noexcept;

  /**
   * @brief Called after commit to compact the log.
   */
  void maybe_compact() noexcept;

  /**
   * @brief Send a MessageT out to other nodes.
   * @param [in] msg The message to send.
   */
  void emit(const MessageT &msg) noexcept {
    detail::call_if(m_cbs.m_send, msg);
  }

  /** @brief The callback supplied by the rest of the application. */
  CallbacksT m_cbs{};

  /** @brief Our EUI-64 */
  NodeId m_self_id{BAD_NODE};

  /** @brief The number of nodes in the cluster. */
  std::size_t m_n_nodes{0};

  /** @brief Our peers (including self). */
  std::array<Peer, Cfg::MAX_NODES> m_nodes{};

  /** @brief The current raft term. */
  Term m_current_term{0};

  /** @brief Who we last voted for. */
  NodeId m_voted_for{BAD_NODE};

  /** @brief The log ring. */
  LogRing<Cfg> m_log;

  /** @brief last committed index. */
  Index m_commit_index{0};

  /** @brief The last applied index. */
  Index m_last_applied{0};

  /** @brief Our current roll. */
  State m_state{State::FOLLOWER};

  /** @brief The ID of the leader node. */
  NodeId m_current_leader{BAD_NODE};

  /** @brief The last known leader. */
  NodeId m_leader_hint{BAD_NODE};

  /** @brief The deadline for the election. */
  Time m_election_deadline{0};

  /** @brief The last time we got a heart beat from the leader. */
  Time m_last_heartbeat_sent{0};

  /**
   * @brief The number of votes that have been granted to us to become leader.
   */
  std::size_t m_votes_granted{0};

  /**
   * @brief Chunk bytes assembled at their offsets; full snapshot once
   * complete
   */
  std::array<std::byte, Cfg::SNAPSHOT_MAX> m_snap_buf{};

  /** @brief The number of bytes received so far. */
  std::uint32_t m_snap_len{0};

  /** @brief The last included index of the snapshot received. */
  Index m_snap_index{0};

  /** @brief The last included index of the snapshot. */
  Term m_snap_term{0};

  /** @brief Are we currently running the server. */
  bool m_running{false};

  /** @brief Is the sever in a valid state. */
  bool m_valid{false};
};

/**
 * INCLUDED IMPLEMENTATION FILES.
 */
#include "server_driver_impl.inl"
#include "server_election_impl.inl"
#include "server_replication_impl.inl"
#include "server_snapshot_impl.inl"

} // namespace fog::raft
