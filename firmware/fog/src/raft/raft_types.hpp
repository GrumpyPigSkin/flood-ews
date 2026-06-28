#pragma once

#include "common/expected.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <variant>

/**
 * @brief The types in here are based off the raft specification here:
 * https://raft.github.io/raft.pdf
 */

namespace fog::raft {

/**
 * @brief To keep the log configurable I use a policy based design, this allows
 * the separation of logic and parametrisation.
 */
struct DefaultConfig {
  static constexpr std::size_t MAX_NODES = 3;       // servers in the cluster
  static constexpr std::size_t LOG_CAPACITY = 64;   // in-memory log ring size
  static constexpr std::size_t MAX_ENTRY_DATA = 64; // payload bytes per entry
  static constexpr std::size_t MAX_APPEND_ENTRIES = 8; // entries per AE RPC
  static constexpr std::size_t SNAPSHOT_MAX = 512;     // reassembly buffer
  static constexpr std::size_t SNAPSHOT_CHUNK = 256;   // bytes per IS chunk

  /**
   * @brief Election timeout is chosen randomly in [min, min+spread] ms
   * Heartbeat must be << election timeout. The choice for a large timeout here
   * is that sensor readings happen every 30-300s, so a ms timeout doesn't gain
   * much and just increase the amount of thread usage.
   */
  static constexpr std::uint32_t ELECTION_TIMEOUT_MIN_MS = 2500;
  static constexpr std::uint32_t ELECTION_TIMEOUT_SPREAD_MS = 1000;
  static constexpr std::uint32_t HEARTBEAT_INTERVAL_MS = 1000;
};

using Term = std::uint64_t;   // terms increase monotonically (5.1)
using Index = std::uint64_t;  // log index; first real entry is 1
using NodeId = std::uint64_t; // This nodes EUI-64
using Time = std::uint64_t;   // monotonic milliseconds

/**
 * @brief Bad node all F's
 */
inline constexpr NodeId BAD_NODE = static_cast<NodeId>(~0ULL);

/**
 * @brief State for the raft node, it can be either a FOLLOWER, LEADER or
 * CANDIDATE.
 */
enum class State : std::uint8_t { FOLLOWER, CANDIDATE, LEADER };

/**
 * @brief Get the state as a string for logging.
 * @param [in] state
 * @return constexpr std::string_view
 */
[[nodiscard]] constexpr std::string_view to_string(const State state) noexcept {
  switch (state) {
  case State::FOLLOWER:
    return "FOLLOWER";
  case State::CANDIDATE:
    return "CANDIDATE";
  case State::LEADER:
    return "LEADER";
  }
  return "UKNOWN";
}

enum class Error : std::uint8_t {
  OK = 0,
  NOT_LEADER, // operation requires leadership
  LOG_FULL,   // in-memory log ring is full; snapshot needed
  SHUTDOWN,   // server has been stopped
  BAD_ARG,
  NOT_FOUND,
};

/**
 * @brief Check a return value is okay.
 * @param [in] e Error code.
 * @return true if e == Error::OK
 */
[[nodiscard]] constexpr bool ok(Error err) noexcept { return err == Error::OK; }

/**
 * @brief Result type for returning either the actual type T or the error type
 * Error.
 * @tparam T
 */
template <typename T> using Result = tl::expected<T, Error>;

/**
 * @brief The current entry type, heartbeats are NOOPS.
 */
enum class EntryType : std::uint8_t {
  COMMAND, // opaque state-machine command
  NOOP,    // blank no-op committed at start of term (8)
};

/**
 * @brief Entry type for the log.
 * @tparam Cfg for MAX_ENTRY_DATA.
 */
template <typename Cfg = DefaultConfig> struct Entry {
  Term m_term{};   // term entry was created in
  Index m_index{}; // position in the log
  EntryType m_type{EntryType::COMMAND};
  std::uint16_t m_data_len{};
  std::array<std::byte, Cfg::MAX_ENTRY_DATA> m_data{};

  /**
   * @brief Helper to get a span to the payload.
   * @return std::span<const std::byte>
   */
  [[nodiscard]] std::span<const std::byte> payload() const noexcept {
    return {m_data.data(), m_data_len};
  }
};

/**
 * @brief RequestVote RPC (5.2, 5.4).
 */
struct RequestVote {
  Term term{};            // candidate's term
  NodeId candidate_id{};  // candidate requesting the vote
  Index last_log_index{}; // index of candidate's last log entry
  Term last_log_term{};   // term of candidate's last log entry
};

/**
 * @brief Response for request vote.
 */
struct RequestVoteResp {
  Term term{};         // currentTerm, for candidate to update itself
  bool vote_granted{}; // true means candidate received the vote
};

/**
 * @brief AppendEntries RPC (5.3); empty entries == heartbeat (5.2)
 * @tparam Cfg
 */
template <typename Cfg = DefaultConfig> struct AppendEntries {
  Term m_term{};            // leader's term
  NodeId m_leader_id{};     // so follower can redirect clients
  Index m_prev_log_index{}; // index of entry preceding new ones
  Term m_prev_log_term{};   // term of prev_log_index entry
  Index m_leader_commit{};  // leader's commitIndex
  std::uint16_t n_entries{};
  std::array<Entry<Cfg>, Cfg::MAX_APPEND_ENTRIES> m_entries{};

  [[nodiscard]] std::span<const Entry<Cfg>> view() const noexcept {
    return {m_entries.data(), n_entries};
  }
};

/**
 * @brief Response for append entries.
 */
struct AppendEntriesResp {
  Term m_term{};    // currentTerm, for leader to update itself
  bool m_success{}; // true if follower matched prev_log_*
  // Optimisation (5.3): on failure the follower hints where to back up. But it
  // does not in the documents that this is unlikely to ever happen.
  Index m_conflict_index{}; // first index it stores for the conflict term,
                            // or follower's log end+1 if too short
  Term m_conflict_term{};   // term of the conflicting entry, 0 if none
  Index m_last_log_index{}; // last entry the follower now has (for matchIndex)
};

/**
 * @brief InstallSnapshot RPC (7, Figure 13). offset/done kept for protocol
 * fidelity so chunking can be added later without an API break.
 * @tparam Cfg Policy for snapshot chunk.
 */
template <typename Cfg = DefaultConfig> struct InstallSnapshot {
  Term m_term{}; // leader's term
  NodeId m_leader_id{};
  Index m_last_included_index{}; // the snapshot replaces all entries up through
                                 // and including this index.
  Term m_last_included_term{};   // term of last_included_index
  std::uint32_t m_offset{};      // byte offset of this chunk
  std::uint32_t m_total_len{};   // total snapshot size in bytes
  std::uint16_t m_data_len{};    // bytes in this chunk
  bool m_done{};                 // last chunk
  std::array<std::byte, Cfg::SNAPSHOT_CHUNK> m_data{};
};

/**
 * @brief Response for InstallSnapshot RPC.
 */
struct InstallSnapshotResp {
  Term m_term{};                 // currentTerm, for leader to update itself
  Index m_last_included_index{}; // echo so leader can advance nextIndex
};

/**
 * @brief Tagged union the transport hands in/out.
 * @tparam Cfg Policy to pass onto sub classes.
 */
template <typename Cfg = DefaultConfig> struct Message {
  using Payload =
      std::variant<RequestVote, RequestVoteResp, AppendEntries<Cfg>,
                   AppendEntriesResp, InstallSnapshot<Cfg>,
                   InstallSnapshotResp>; // The payload to send/receive.
  NodeId from{};                         // sender node id
  NodeId to{}; // recipient node id (== self for inbound)
  Payload payload{};
};

/**
 * @brief Per-peer bookkeeping for the leader.
 */
struct Peer {
  NodeId id{BAD_NODE};
  Index next_index{1};  // next entry to send (init: leader last + 1)
  Index match_index{0}; // highest entry known replicated
  Time last_contact{};  // last time we heard from this peer
  bool vote_granted{};  // did this peer vote for us this term
  bool is_self{};
};

} // namespace fog::raft
