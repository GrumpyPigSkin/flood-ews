#pragma once

/**
 * Test harness and helpers for raft tests, sets up a virtual raft cluster using
 * three threads. These can then be manipulated and the state of the cluster
 * monitored for it's behaviour.
 */

#include "../../fog/src/raft/raft_types.hpp"
#include "../../fog/src/raft/server/server.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <format>
#include <gtest/gtest.h>
#include <mutex>
#include <optional>
#include <print>
#include <random>
#include <ranges>
#include <span>
#include <stop_token>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace fog::raft::test {

// ============================================================================
// Configuration
// ============================================================================

constexpr std::size_t NUM_NODES = 3;
constexpr auto NODE_INDICIES = std::views::iota(std::size_t{0}, NUM_NODES);
constexpr std::array<NodeId, NUM_NODES> NODES = {1, 2, 3};
constexpr auto SHORT_SLEEP_INTERVAL = std::chrono::milliseconds(5);

/**
 * @brief Tick well below the heartbeat interval so scheduler jitter cannot
 * push a heartbeat past a follower's election deadline.
 */
constexpr auto TICK = std::chrono::milliseconds(10);

/**
 * @brief Shrunk timeouts for fast tests.
 */
struct TestConfig {
  static constexpr std::size_t MAX_NODES = 3;
  static constexpr std::size_t LOG_CAPACITY = 64;
  static constexpr std::size_t MAX_ENTRY_DATA = 96;
  static constexpr std::size_t MAX_APPEND_ENTRIES = 8;
  static constexpr std::size_t SNAPSHOT_MAX = 512;
  static constexpr std::size_t SNAPSHOT_CHUNK = 256;
  static constexpr std::size_t SNAPSHOT_THRESHOLD = LOG_CAPACITY / 2;

  static constexpr std::uint32_t ELECTION_TIMEOUT_MIN_MS = 250;
  static constexpr std::uint32_t ELECTION_TIMEOUT_SPREAD_MS = 150;
  static constexpr std::uint32_t HEARTBEAT_INTERVAL_MS = 50;
};

using ServerT = Server<TestConfig>;
using MessageT = ServerT::MessageT;
using EntryT = ServerT::EntryT;

// ============================================================================
// Clock and small helpers
// ============================================================================

/** @brief Monotonic milliseconds since the first call. */
inline Time now_ms() {
  static const auto start = std::chrono::steady_clock::now();
  return static_cast<Time>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - start)
          .count());
}

/** @brief Map a NodeId to its slot, or NUM_NODES if unknown. */
constexpr std::size_t index_of(const NodeId nid) noexcept {
  for (const auto idx : NODE_INDICIES) {
    if (NODES[idx] == nid) {
      return idx;
    }
  }
  return NUM_NODES;
}

/**
 * @brief FNV hash constants:
 * https://en.wikipedia.org/wiki/Fowler%E2%80%93Noll%E2%80%93Vo_hash_function
 */
constexpr std::uint64_t FNV_OFFSET_BASIS = 14695981039346656037ULL;
constexpr std::uint64_t FNV_PRIME = 1099511628211ULL;

/**
 * @brief FNV hash the payload for checking:
 * https://en.wikipedia.org/wiki/Fowler%E2%80%93Noll%E2%80%93Vo_hash_function
 */
constexpr std::uint64_t payload_hash(std::span<const std::byte> data) noexcept {
  std::uint64_t hash = FNV_OFFSET_BASIS;
  for (const std::byte byte : data) {
    hash ^= static_cast<std::uint64_t>(byte);
    hash *= FNV_PRIME;
  }
  return hash;
}

/**
 * @brief EntryType as text, so apply lines read rather than printing an
 * integer.
 */
[[nodiscard]] constexpr std::string_view
to_string(const EntryType type) noexcept {
  switch (type) {
  case EntryType::NOOP:
    return "NOOP";
  case EntryType::SENSOR_DATA:
    return "SENSOR_DATA";
  case EntryType::EGRESS_MSG:
    return "EGRESS_MSG";
  }
  return "UNKNOWN";
}

// ============================================================================
// Link model
// ============================================================================

/**
 * @brief Per-node link state. This splits the send an receive for asymmetric
 * tests where a node could send but no receive.
 */
struct LinkState {

  /** @brief Can this node send. */
  std::atomic<bool> m_can_send{true};

  /** @brief Can this node receive. */
  std::atomic<bool> m_can_recv{true};

  /** @brief Stop the nodes send and receive. */
  void kill() {
    m_can_send.store(false);
    m_can_recv.store(false);
  }

  /** @brief Allow the node to send and receive. */
  void revive() {
    m_can_send.store(true);
    m_can_recv.store(true);
  }

  /** @brief Can the node send a receive. */
  [[nodiscard]] bool alive() const {
    return m_can_send.load() && m_can_recv.load();
  }
};

/**
 * @brief Probabilistic fault injection applied to every message. Raft must
 * tolerate all of these.
 */
struct FaultConfig {
  /** @brief The probability a message is dropped. */
  double m_drop_prob{0.0};

  /** @brief The probability a message is duplicated. */
  double m_duplicate_prob{0.0};

  /** @brief The probability a message is delayed. */
  double m_delay_prob{0.0};

  /** @brief Max extra delivery delay in ticks when a message is delayed. */
  std::uint32_t m_max_delay_ticks{3};
};

// ============================================================================
// Inbox
// ============================================================================

/**
 * @brief Thread-safe message queue, one per node.
 *
 * Delayed messages carry a release time; drain_ready() leaves them queued until
 * due.
 */
class Inbox {
public:
  /**
   * @brief Add a message onto the queue.
   * @param [in] msg The message.
   * @param [in] release_at The time to release the message at.
   */
  void put(const MessageT &msg, const Time release_at = 0) {
    {
      std::scoped_lock lock(m_mtx);
      m_queue.push_back(Pending{msg, release_at});
    }
    m_cv.notify_one();
  }

  /**
   * @brief Block until something arrives, stop is requested, or the timeout
   * expires; then move out every message whose release time has passed.
   */
  std::vector<MessageT> drain_ready(const std::stop_token &token,
                                    const std::chrono::milliseconds timeout) {
    std::unique_lock lock(m_mtx);
    m_cv.wait_for(lock, timeout,
                  [&] { return !m_queue.empty() || token.stop_requested(); });

    const Time now = now_ms();
    std::vector<MessageT> ready;
    std::deque<Pending> held;

    for (auto &pending : m_queue) {
      if (pending.m_release_at <= now) {
        ready.push_back(pending.m_msg);
      } else {
        held.push_back(pending);
      }
    }
    // Re-add the held messages back to the queue.
    m_queue = std::move(held);
    return ready;
  }

  /** @brief Discard everything queued. */
  void clear() {
    std::scoped_lock lock(m_mtx);
    m_queue.clear();
  }

  /** @brief Wake any waiter so a stop request is noticed promptly. */
  void wake() { m_cv.notify_all(); }

private:
  /** @brief A pending message. */
  struct Pending {
    MessageT m_msg;
    Time m_release_at{};
  };

  /** @brief Mutex to protect state. */
  std::mutex m_mtx;

  /** @brief Condition-var to be woken from. */
  std::condition_variable m_cv;

  /** @brief The internal message queue. */
  std::deque<Pending> m_queue;
};

// ============================================================================
// Command queue
// ============================================================================

/**
 * @brief The command type and data.
 */
struct Command {
  EntryType m_type{EntryType::SENSOR_DATA};
  std::vector<std::byte> m_data;
};

/**
 * @brief A simple thread safe queue for sending commands.
 */
class CommandQueue {
public:
  /** @brief Push a command. */
  void push(Command cmd) {
    std::scoped_lock lock(m_mtx);
    m_queue.push_back(std::move(cmd));
  }

  /** @brief drain all commands. */
  std::deque<Command> drain() {
    std::scoped_lock lock(m_mtx);
    return std::exchange(m_queue, {});
  }

private:
  /** @brief Mutex to protext state. */
  std::mutex m_mtx;

  /** @brief The underlying queue. */
  std::deque<Command> m_queue;
};

// ============================================================================
// Observed history
// ============================================================================

/** @brief One applied entry, recorded for the safety checks. */
struct Applied {
  Index m_index{};
  Term m_term{};
  EntryType m_type{};
  std::uint64_t m_hash{};

  friend bool operator<=>(const Applied &, const Applied &) = default;
};

} // namespace fog::raft::test

/** @brief Formatter for printing Applied. */
template <> struct std::formatter<fog::raft::test::Applied> {
  constexpr auto parse(std::format_parse_context &ctx) { return ctx.begin(); }
  auto format(const fog::raft::test::Applied &a,
              std::format_context &ctx) const {
    return std::format_to(ctx.out(), "idx={} term={} type={} hash={:#x}",
                          a.m_index, a.m_term,
                          fog::raft::test::to_string(a.m_type), a.m_hash);
  }
};

namespace fog::raft::test {

/**
 * @brief Everything the harness observes, shared across node threads.
 */
struct Observed {
  /** @brief The rolls of each node. */
  std::array<std::atomic<State>, NUM_NODES> m_roles{};

  /** @brief The applied entries for each node. */
  std::array<std::vector<Applied>, NUM_NODES> m_applied{};

  /** @brief Mutex to protect m_applied. */
  std::mutex m_applied_mtx;

  /**
   * @brief Persisted (term, voted_for) per node, this is the only state
   * persisted on the raft nodes.
   */
  struct Persisted {
    Term m_term{0};
    NodeId m_voted_for{BAD_NODE};
  };

  /** @brief The persisted entries, each node only stores the latest. */
  std::array<Persisted, NUM_NODES> m_persisted{};

  /** @brief Mutex to protect m_persisted. */
  std::mutex m_persisted_mtx;

  /** @brief AppendEntries sent per node, for measuring backtracking cost. */
  std::array<std::atomic<std::uint64_t>, NUM_NODES> m_ae_sent{};

  /** @brief Suppress per-message logging during soak runs. */
  std::atomic<bool> m_verbose{true};

  /** @brief Reset the state of Observed. */
  void reset() {
    for (const auto idx : NODE_INDICIES) {
      m_roles[idx].store(State::FOLLOWER);
      m_ae_sent[idx].store(0);
      m_applied[idx].clear();
      m_persisted[idx] = Persisted{};
    }
  }
};

// ============================================================================
// Cluster
// ============================================================================

/**
 * @brief Hook allowing a test to add callbacks the shared harness does not
 * bind, snapshot save/load in particular.
 */
using CallbackHook = std::function<void(std::size_t, Callbacks<TestConfig> &)>;

/**
 * @brief Owns the servers, threads, inboxes and link state for one cluster.
 */
class Cluster {
public:
  /**
   * @brief Constructor.
   * @param [in] faults Fault probabilities
   * @param [in] seed RNG seed.
   * @param [in] hook Callback hook.
   */
  explicit Cluster(const FaultConfig faults = {}, const std::uint32_t seed = 1,
                   CallbackHook hook = {})
      : m_faults(faults), m_seed(seed), m_hook(std::move(hook)) {
    m_observed.reset();
    // Put node state in a good state.
    for (const auto idx : NODE_INDICIES) {
      m_links[idx].revive();
      m_servers.emplace_back(NODES[idx], NODES, make_cbs(idx));
    }
  }

  /** @brief Destructor. */
  ~Cluster() { shutdown(); }

  /** @brief Deleted copy/move constructors. */
  Cluster(const Cluster &) = delete;
  Cluster &operator=(const Cluster &) = delete;
  Cluster(Cluster &&) = delete;
  Cluster &operator=(Cluster &&) = delete;

  /** @brief Verify construction, then start every node thread. */
  [[nodiscard]] bool start() {

    for (const auto idx : NODE_INDICIES) {
      if (!m_servers[idx].valid()) {
        std::println(stderr, "FAIL: server {} constructed invalid", idx);
        return false;
      }
    }

    for (const auto idx : NODE_INDICIES) {
      m_threads[idx] = std::jthread([this, idx](std::stop_token token) {
        node_loop(std::move(token), idx);
      });
    }

    return true;
  }

  /** @brief Stop every thread and wake it out of its condvar wait. */
  void shutdown() {

    for (auto &thread : m_threads) {
      if (thread.joinable()) {
        thread.request_stop();
      }
    }

    for (auto &inbox : m_inboxes) {
      inbox.wake();
    }

    for (auto &thread : m_threads) {
      if (thread.joinable()) {
        thread.join();
      }
    }
  }

  /** @brief `node` neither sends nor receives. */
  void kill(const std::size_t node) {
    m_links[node].kill();
    m_inboxes[node].clear();
    log("=== partitioned node {} ===", node);
  }

  /** @brief Allow send/recv on the given `node`. */
  void revive(const std::size_t node) {
    m_links[node].revive();
    log("=== revived node {} ===", node);
  }

  /** @brief `node` still receives, but its replies are dropped. */
  void mute_send(const std::size_t node) {
    m_links[node].m_can_send.store(false);
    log("=== muted send from node {} ===", node);
  }

  /** @brief `node` still sends, but its receives are dropped. */
  void mute_recv(const std::size_t node) {
    m_links[node].m_can_recv.store(false);
    m_inboxes[node].clear();
    log("=== muted recv to node {} ===", node);
  }

  /** @brief Can `node` both send and receive. */
  [[nodiscard]] bool alive(const std::size_t node) const {
    return m_links[node].alive();
  }

  /**
   * @brief Crash-restart `node`.
   * Only (term, voted_for) survives.
   */
  void restart(const std::size_t node) {
    m_links[node].kill();
    if (m_threads[node].joinable()) {
      m_threads[node].request_stop();
      m_inboxes[node].wake();
      m_threads[node].join();
    }
    m_inboxes[node].clear();

    Observed::Persisted saved;
    {
      std::scoped_lock lock(m_observed.m_persisted_mtx);
      saved = m_observed.m_persisted[node];
    }

    {
      std::scoped_lock lock(m_observed.m_applied_mtx);
      m_observed.m_applied[node].clear();
    }

    m_servers[node] = ServerT(NODES[node], NODES, make_cbs(node));
    m_servers[node].restore_state(saved.m_term, saved.m_voted_for);
    m_observed.m_roles[node].store(State::FOLLOWER);

    if (m_on_restart) {
      m_on_restart(node);
    }

    m_links[node].revive();
    m_threads[node] = std::jthread([this, node](std::stop_token token) {
      node_loop(std::move(token), node);
    });

    log("=== restarted node {} (term={} voted_for={}) ===", node, saved.m_term,
        saved.m_voted_for);
  }

  void on_restart(std::function<void(std::size_t)> func) {
    m_on_restart = std::move(func);
  }

  /**
   * @brief Queue a message to `node`.
   * @param [in] node The node to send to, not the ID.
   * @param [in] data The data to send.
   * @param [in] type The command type.
   */
  void submit(const std::size_t node, std::vector<std::byte> data,
              const EntryType type = EntryType::SENSOR_DATA) {
    m_commands[node].push(Command{type, std::move(data)});
  }

  /**
   * @brief Wait until exactly one live node reports LEADER.
   * Requiring exactly one matters, having two leaders would be a safety issue.
   */
  std::optional<std::size_t> await_leader(
      const std::chrono::milliseconds timeout = std::chrono::seconds(10)) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
      std::optional<std::size_t> found;
      std::size_t count = 0;
      for (std::size_t idx : NODE_INDICIES) {
        if (m_links[idx].alive() &&
            m_observed.m_roles[idx].load() == State::LEADER) {
          ++count;
          found = idx;
        }
      }
      // Make sure we have just one leader.
      if (count == 1) {
        return found;
      }
      std::this_thread::sleep_for(SHORT_SLEEP_INTERVAL);
    }
    return std::nullopt;
  }

  /** @brief Assert no live node claims leadership for the whole window. */
  [[nodiscard]] bool assert_no_leader(const std::chrono::milliseconds window) {
    const auto deadline = std::chrono::steady_clock::now() + window;
    while (std::chrono::steady_clock::now() < deadline) {
      for (std::size_t idx : NODE_INDICIES) {
        if (m_links[idx].alive() &&
            m_observed.m_roles[idx].load() == State::LEADER) {
          std::println(stderr, "FAIL: node {} leads without a quorum", idx);
          return false;
        }
      }
      std::this_thread::sleep_for(SHORT_SLEEP_INTERVAL);
    }
    return true;
  }

  /** @brief Get the role for `node`. */
  [[nodiscard]] State role(const std::size_t node) const {
    return m_observed.m_roles[node].load();
  }

  /** @brief Get the number of applied messages for `node`. */
  [[nodiscard]] std::size_t applied_count(const std::size_t node) {
    std::scoped_lock lock(m_observed.m_applied_mtx);
    return m_observed.m_applied[node].size();
  }

  /** @brief Get the number of append entries for `node`. */
  [[nodiscard]] std::uint64_t ae_sent(const std::size_t node) const {
    return m_observed.m_ae_sent[node].load();
  }

  /** @brief Get the observed state. */
  Observed &observed() { return m_observed; }

  /** @brief Get the raft server per node. */
  ServerT &server(const std::size_t node) { return m_servers[node]; }

  /** @brief Set whether or not messages should be verbose. */
  void set_verbose(const bool verbose) { m_observed.m_verbose.store(verbose); }

  /**
   * @brief Wait until every live node has applied at least `count` entries.
   * @return true if they converged before the timeout.
   */
  bool await_applied(const std::size_t count,
                     const std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
      bool all = true;
      {
        std::scoped_lock lock(m_observed.m_applied_mtx);
        for (std::size_t idx : NODE_INDICIES) {
          if (m_links[idx].alive() &&
              m_observed.m_applied[idx].size() < count) {
            all = false;
          }
        }
      }
      if (all) {
        return true;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
  }

private:
  /** @brief Print only when verbose. */
  template <typename... Args>
  void log(const std::format_string<Args...> fmt, Args &&...args) {
    if (m_observed.m_verbose.load()) {
      std::println(fmt, std::forward<Args>(args)...);
    }
  }

  /** @brief Make the callbacks for a node `self`. */
  Callbacks<TestConfig> make_cbs(const std::size_t self) {
    Callbacks<TestConfig> cbs{
        .m_send = [this, self](const MessageT &msg) { route(self, msg); },
        .m_apply =
            [this, self](const EntryT &entry) {
              {
                std::scoped_lock lock(m_observed.m_applied_mtx);
                m_observed.m_applied[self].push_back(
                    Applied{entry.m_index, entry.m_term, entry.m_type,
                            payload_hash(entry.payload())});
              }
              log("[{:>6} ms][node {}] apply idx={} term={} type={}", now_ms(),
                  self, entry.m_index, entry.m_term, to_string(entry.m_type));
            },
        .m_persist_state =
            [this, self](const Term term, const NodeId voted_for) {
              std::scoped_lock lock(m_observed.m_persisted_mtx);
              m_observed.m_persisted[self] =
                  Observed::Persisted{term, voted_for};
            },
        .m_now = &now_ms,
        .m_rand = [self, this]() mutable -> std::uint32_t {
          const std::seed_seq seq{static_cast<std::uint32_t>(self), m_seed};
          thread_local std::mt19937 rng(seq);
          return rng();
        },
        .m_on_state_change =
            [this, self](const State old_state, const State new_state) {
              m_observed.m_roles[self].store(new_state);
              log("[{:>6} ms][node {}] {} -> {}", now_ms(), self,
                  to_string(old_state), to_string(new_state));
            },
    };

    // Let a test bind anything the shared harness leaves unset.
    if (m_hook) {
      m_hook(self, cbs);
    }
    return cbs;
  }

  /** @brief Apply link state and fault injection, then enqueue. */
  void route(const std::size_t self, const MessageT &msg) {
    // Make sure the node can send a message.
    if (!m_links[self].m_can_send.load(std::memory_order_relaxed)) {
      return;
    }

    // Make sure it has a valid ID.
    const std::size_t dst = index_of(msg.to);
    if (dst >= NUM_NODES) {
      return;
    }

    // Make sure the receiver can receive.
    if (!m_links[dst].m_can_recv.load(std::memory_order_relaxed)) {
      return;
    }

    // Increment the number of append entries sent if this is the message type.
    if (std::holds_alternative<AppendEntries<TestConfig>>(msg.payload)) {
      m_observed.m_ae_sent[self].fetch_add(1, std::memory_order_relaxed);
    }

    // The fault RNG is shared across threads, so it needs its own lock.
    double roll_drop = 0.0;
    double roll_dup = 0.0;
    double roll_delay = 0.0;
    std::uint32_t delay_ticks = 0;
    {
      std::scoped_lock lock(m_fault_mtx);
      std::uniform_real_distribution<double> unit(0.0, 1.0);
      roll_drop = unit(m_fault_rng);
      roll_dup = unit(m_fault_rng);
      roll_delay = unit(m_fault_rng);
      // Get the delay time.
      if (m_faults.m_max_delay_ticks > 0) {
        std::uniform_int_distribution<std::uint32_t> ticks(
            1, m_faults.m_max_delay_ticks);
        delay_ticks = ticks(m_fault_rng);
      }
    }

    // Message is dropped.
    if (roll_drop < m_faults.m_drop_prob) {
      return;
    }

    // Add a delay if the potability met.
    Time release_at = 0;
    if (roll_delay < m_faults.m_delay_prob) {
      release_at = now_ms() + (delay_ticks * static_cast<Time>(TICK.count()));
    }

    m_inboxes[dst].put(msg, release_at);

    // Duplicate a message.
    if (roll_dup < m_faults.m_duplicate_prob) {
      m_inboxes[dst].put(msg, release_at);
    }
  }

  /**
   * @brief Run per server, handles the servers periodic tick  and messages.
   * @param [in] token The stop token.
   * @param [in] self This nodes id.
   */
  void node_loop(std::stop_token token, const std::size_t self) {
    // Get the server for this node.
    ServerT &srv = m_servers[self];
    srv.start();

    // Loop until stop is requested.
    while (!token.stop_requested()) {

      auto batch = m_inboxes[self].drain_ready(token, TICK);

      // Are we allowed to receive?
      if (m_links[self].m_can_recv.load(std::memory_order_relaxed)) {
        for (const auto &msg : batch) {
          srv.handle(msg);
        }
      }

      // Submissions are dropped while the node is partitioned.
      if (m_links[self].alive()) {
        for (auto &cmd : m_commands[self].drain()) {
          const auto result = srv.submit(cmd.m_type, cmd.m_data);
          if (!result) {
            log("[{:>6} ms][node {}] submit rejected (err={})", now_ms(), self,
                static_cast<std::uint8_t>(result.error()));
          }
        }
      } else {
        m_commands[self].drain();
      }

      // Always run periodic.
      srv.periodic();
    }

    srv.stop();
  }

  /** @brief Fault configuration. */
  FaultConfig m_faults;

  /** @brief RNG seed. */
  std::uint32_t m_seed;

  /** @brief User supplied hook. */
  CallbackHook m_hook;

  /** @brief The raft servers. */
  std::deque<ServerT> m_servers;

  /** @brief Send/receive queues for each server. */
  std::array<Inbox, NUM_NODES> m_inboxes;

  /** @brief Command queue per server. */
  std::array<CommandQueue, NUM_NODES> m_commands;

  /** @brief Link state per server. */
  std::array<LinkState, NUM_NODES> m_links;

  /** @brief Thread for each server. */
  std::array<std::jthread, NUM_NODES> m_threads;

  /** @brief The observed state. */
  Observed m_observed;

  /** @brief Mutex and RNG for fault. */
  std::mutex m_fault_mtx;
  std::mt19937 m_fault_rng{m_seed};

  /** @brief Called when the node has been stopped during a restart. */
  std::function<void(std::size_t)> m_on_restart;
};

// ============================================================================
// Invariant checks
// ============================================================================

/** @brief Each node must apply indices in order, no gaps, no repeats. */
inline bool assert_apply_order(Observed &observed) {
  std::scoped_lock lock(observed.m_applied_mtx);
  bool okay = true;
  for (const auto idx : NODE_INDICIES) {
    const auto &log = observed.m_applied[idx];
    for (std::size_t k = 1; k < log.size(); ++k) {
      if (log[k].m_index <= log[k - 1].m_index) {
        std::println(
            stderr,
            "FAIL: node {} apply went backward or repeated: {} after {}", idx,
            log[k].m_index, log[k - 1].m_index);
        okay = false;
      }
    }
  }
  return okay;
}

/**
 * @brief State machine safety: if two nodes applied an entry at index i, it is
 * the same entry.
 */
inline bool assert_applied_agree(Observed &observed) {
  std::scoped_lock lock(observed.m_applied_mtx);
  bool okay = true;
  // Index a node's applies by log index so nodes that began at different points
  // are compared where they actually overlap.
  auto by_index = [](const std::vector<Applied> &log) {
    std::unordered_map<Index, const Applied *> m;
    for (const auto &a : log) {
      m[a.m_index] = &a;
    }
    return m;
  };
  for (std::size_t x = 0; x < NUM_NODES; ++x) {
    for (std::size_t y = x + 1; y < NUM_NODES; ++y) {
      const auto lhs = by_index(observed.m_applied[x]);
      for (const auto &[idx, applied] : lhs) {
        const auto &ylog = observed.m_applied[y];
        const auto it = std::ranges::find(ylog, idx, &Applied::m_index);
        if (it != ylog.end() && !(*it == *applied)) {
          std::println(stderr, "FAIL: divergent apply at index {} ({})", idx, x,
                       *applied);
          okay = false;
        }
      }
    }
  }
  return okay;
}

/** @brief Run the standard end-of-test invariants. */
inline int check_invariants(Observed &observed) {
  int failures = 0;
  if (!assert_apply_order(observed)) {
    ++failures;
  }
  if (!assert_applied_agree(observed)) {
    ++failures;
  }
  return failures;
}

/** @brief Print the pass/fail banner and return the process exit code. */
inline int report(const std::string_view name, const int failures) {
  if (failures == 0) {
    std::println("=== {}: PASS ===", name);
    return 0;
  }
  std::println("=== {}: {} FAILURE(S) ===", name, failures);
  return 1;
}

// Helper for seting up tests.
struct TestContext : public ::testing::Test {

  /** @brief The raft cluster. */
  std::unique_ptr<Cluster> m_cluster;

  /** @brief The leader on startup. */
  std::size_t m_leader_id;

  /** @brief Gtest setup. */
  void SetUp() override {
    m_cluster = std::make_unique<Cluster>();
    if (!m_cluster->start()) {
    }

    auto leader_opt = m_cluster->await_leader();

    ASSERT_TRUE(leader_opt.has_value()) << "no initial leader";

    m_leader_id = leader_opt.value();
  }

  /** @brief Shutdown and check invariants. */
  int finish_and_check() const {
    m_cluster->shutdown();
    return check_invariants(m_cluster->observed());
  }

  /** @brief Submit a batch to raft. */
  void submit_batch(const std::byte base_val, const int count = 3) const {
    for (int i = 0; i < count; ++i) {
      m_cluster->submit(m_leader_id, {static_cast<std::byte>(
                                         std::to_underlying(base_val) + i)});
    }
  }

  /** @brief Override m_cluster and id and provide new args.  */
  void make_new_cluster(const FaultConfig faults = {},
                        const std::uint32_t seed = 1, CallbackHook hook = {}) {

    m_cluster->shutdown();

    m_cluster = std::make_unique<Cluster>(faults, seed, std::move(hook));

    if (!m_cluster->start()) {
    }

    auto leader_opt = m_cluster->await_leader();

    ASSERT_TRUE(leader_opt.has_value()) << "no leader";

    m_leader_id = leader_opt.value();
  }

  /** @brief Print the applied count. */
  void print_applied() const {
    for (const auto idx : NODE_INDICIES) {
      std::println("node {} applied {} entries", idx,
                   m_cluster->applied_count(idx));
    }
  }
};

} // namespace fog::raft::test
