/**
 * Randomised fault injection with continuous invariant checking.
 *
 * Usage: ./test_soak       # random seed, ~60s
 * ./test_soak 1234567      # replay seed 1234567
 * ./test_soak 1234567 300  # replay for 300 seconds
 *
 * Faults applied:
 *  - random node kill / revive
 *  - asymmetric mutes
 *  - crash-restart
 *  - message drop, duplication, delay and reorder at the link layer
 *
 * Invariants checked continuously:
 *  - never two live leaders at once
 *  - apply order is gapless per node
 *  - nodes agree at every shared apply position
 *
 * The harness never kills more than one node at a time by default, so a quorum
 * always survives and progress should continue. Set ALLOW_QUORUM_LOSS to
 * exercise stalls as well, but raft is not designed to handle the majority of
 * nodes lost.
 */

#include "raft_harness.hpp"
#include "raft_state_machine.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <print>
#include <random>
#include <string_view>
#include <thread>

using namespace fog::raft;
using namespace fog::raft::test;
using namespace std::chrono_literals;

namespace {

/** @brief Allow more than one node to die at one. */
constexpr bool ALLOW_QUORUM_LOSS = false;

/** @brief Signal to listen for the application being killed. */
std::atomic<bool> g_interrupted{false};

/** @brief Action states 0-9. */
constexpr auto MIN_ACTION = 0;
constexpr auto MAX_ACTION = 9;

/** @brief Gap between checks. */
constexpr auto MIN_GAP_MS = 200;
constexpr auto MAX_GAP_MS = 800;

/** @brief Probability for error injection. */
constexpr auto DROP_PROB = 0.02;
constexpr auto DUPLICATE_PROB = 0.02;
constexpr auto DELAY_PROB = 0.05;
constexpr auto MAX_DELAY_TICKS = 4;

constexpr auto SLEEP_TIME = 10s;
constexpr auto SHORT_SLEEP_TIME = 50ms;

constexpr std::uint8_t BYTE_MASK = 0xFF;
constexpr std::uint8_t BYTE_SHIFT = 8;

constexpr std::uint32_t DEFAULT_RUN_TIME = 60;

/** @brief Hijack the kill signal to close down gracefully. */
extern "C" void on_signal(int /*sig*/) {
  g_interrupted.store(true, std::memory_order_relaxed);
}

/** @brief Count nodes currently reachable. */
[[nodiscard]] std::size_t live_count(Cluster &cluster) {
  return static_cast<std::size_t>(
      std::ranges::count_if(NODE_INDICIES, [&cluster](const std::size_t idx) {
        return cluster.alive(idx);
      }));
}

/** @brief Fail fast if two live nodes both claim leadership. */
bool check_single_leader(Cluster &cluster) {
  const auto leaders =
      std::ranges::count_if(NODE_INDICIES, [&cluster](std::size_t idx) {
        return cluster.alive(idx) && cluster.role(idx) == State::LEADER;
      });

  if (leaders > 1) {
    std::println(stderr, "FAIL: {} live nodes claim leadership simultaneously",
                 static_cast<std::size_t>(leaders));
    return false;
  }
  return true;
}

/**
 * @brief Fast, non-throwing string to integer parsing helper using from_chars.
 */
template <typename T>
[[nodiscard]] std::optional<T> parse_number(std::string_view view) noexcept {
  T val{};
  auto [ptr, ec] = std::from_chars(view.data(), view.data() + view.size(), val);
  if (ec == std::errc{}) {
    return val;
  }
  return std::nullopt;
}

} // namespace

int main(int argc, char **argv) {
  (void)std::signal(SIGINT, on_signal);

  // Get the command line args and parse the seen and duration.
  const auto args = std::span(argv, static_cast<std::size_t>(argc));

  const std::uint32_t seed =
      (args.size() > 1) ? parse_number<std::uint32_t>(args[1]).value_or(
                              std::random_device{}())
                        : std::random_device{}();

  const int duration_s =
      (args.size() > 2) ? parse_number<int>(args[2]).value_or(DEFAULT_RUN_TIME)
                        : DEFAULT_RUN_TIME;

  std::println("=== soak: seed={} duration={}s ===", seed, duration_s);
  std::println("=== replay with: ./test_soak {} {} ===", seed, duration_s);

  const FaultConfig faults{.m_drop_prob = DROP_PROB,
                           .m_duplicate_prob = DUPLICATE_PROB,
                           .m_delay_prob = DELAY_PROB,
                           .m_max_delay_ticks = MAX_DELAY_TICKS};

  // Setup the cluster with the snapshotting hook.
  SnapshotTrackers snts;
  snts.reset();
  Cluster cluster(faults, seed, snts.hook());
  // Stop per message logging.
  cluster.set_verbose(false);
  cluster.on_restart([&snts](const std::size_t node) {
    std::println(stderr, "Restarting node {}, clearing state machine", node);
    snts.reset(node);
  });
  if (!cluster.start()) {
    return 1;
  }

  // Create RNGs
  std::mt19937 rng{seed};
  std::uniform_int_distribution<std::size_t> pick_node(0, NUM_NODES - 1);
  std::uniform_int_distribution<int> pick_action(MIN_ACTION, MAX_ACTION);
  std::uniform_int_distribution<int> pick_gap_ms(MIN_GAP_MS, MAX_GAP_MS);

  int failures = 0;
  std::uint64_t submitted = 0;
  std::uint64_t leader_changes = 0;

  std::array<State, NUM_NODES> last_roles{};
  std::ranges::fill(last_roles, State::FOLLOWER);

  // When to finish.
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(duration_s);

  // Let the cluster settle before injecting anything.
  if (!cluster.await_leader()) {
    std::println(stderr, "FAIL: no leader from cold start");
    ++failures;
  }

  // Loop until time out.
  while (std::chrono::steady_clock::now() < deadline &&
         !g_interrupted.load(std::memory_order_relaxed)) {

    // Make sure there is only one leader.
    if (!check_single_leader(cluster)) {
      ++failures;
      break;
    }

    // Track leadership churn for the summary.
    for (std::size_t idx : NODE_INDICIES) {
      const State role = cluster.role(idx);
      if (role != last_roles[idx]) {
        if (role == State::LEADER) {
          ++leader_changes;
        }
        last_roles[idx] = role;
      }
    }

    // Keep writing to whoever leads.
    if (const auto leader = cluster.await_leader(SHORT_SLEEP_TIME)) {
      cluster.submit(*leader, {static_cast<std::byte>(submitted & BYTE_MASK),
                               static_cast<std::byte>(
                                   (submitted >> BYTE_SHIFT) & BYTE_MASK)});
      ++submitted;
    }

    // Inject a fault.
    const std::size_t node = pick_node(rng);
    const int action = pick_action(rng);

    // Only disrupt a node a node if all are alive, unless ALLOW_QUORUM_LOSS ==
    // true.
    const bool would_lose_quorum =
        !ALLOW_QUORUM_LOSS && cluster.alive(node) && live_count(cluster) <= 2;

    const bool can_disrupt = !would_lose_quorum;

    // Cause a random disruption.
    switch (action) {
    case 0:
    case 1:
    case 2:
      if (can_disrupt) {
        cluster.kill(node);
      }
      break;
    case 3:
    case 4:
    case 5:
      cluster.revive(node);
      break;
    case 6:
      if (can_disrupt) {
        cluster.mute_send(node);
      }
      break;
    case 7:
      if (can_disrupt) {
        cluster.mute_recv(node);
      }
      break;
    case 8:
      if (can_disrupt) {
        cluster.restart(node);
      }
      break;
    default:
      break;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(pick_gap_ms(rng)));
  }

  // Heal everything and give the cluster a chance to converge.
  std::println("=== healing all links ===");
  for (std::size_t idx : NODE_INDICIES) {
    cluster.revive(idx);
  }
  std::this_thread::sleep_for(SLEEP_TIME);

  const auto final_leader = cluster.await_leader();
  if (!final_leader) {
    std::println(stderr, "FAIL: cluster did not converge on a leader.");
    ++failures;
  } else {
    std::println("=== converged on node {} ===", *final_leader);

    // It must still accept writes.
    const std::size_t before = cluster.applied_count(*final_leader);
    cluster.submit(*final_leader, {std::byte{BYTE_MASK}});
    std::this_thread::sleep_for(3s);
    if (cluster.applied_count(*final_leader) <= before) {
      std::println(stderr, "FAIL: no progress after healing");
      ++failures;
    }
  }

  // Wait for the state to settle across the nodes, if nodes don't settle than
  // it is hard to tell whether there is a inconsistency between nodes, or
  // whether the nodes just haven't reconciled between themselves.
  bool quiesced = false;
  for (int i = 0; i < 100; ++i) {
    const auto count0 = snts.m_sm[0].m_applied_count;
    const auto count1 = snts.m_sm[1].m_applied_count;
    const auto count2 = snts.m_sm[2].m_applied_count;
    if (count0 == count1 && count1 == count2) {
      quiesced = true;
      break;
    }
    std::this_thread::sleep_for(100ms);
  }
  if (!quiesced) {
    std::println(
        stderr,
        "CONVERGENCE INCONCLUSIVE: nodes did not finish applying within 10s "
        "(counts {}/{}/{}): cannot distinguish lag from divergence",
        snts.m_sm[0].m_applied_count, snts.m_sm[1].m_applied_count,
        snts.m_sm[2].m_applied_count);
  }

  const std::uint64_t digest = snts.m_sm[0].digest();
  bool converged = true;
  for (std::size_t i = 1; i < NUM_NODES; ++i) {
    if (snts.m_sm[i].digest() != digest) {
      std::println(stderr,
                   "FAIL: node {} final state diverges from node 0 "
                   "(digest {:#x} vs {:#x})",
                   i, snts.m_sm[i].digest(), digest);
      converged = false;
    }
  }
  if (converged) {
    std::println("=== all nodes converged to identical final state ===");
  } else {
    ++failures;
  }

  // Print any diverging log entries.
  std::println(stderr, "=== diverging cells (node0 vs node1) ===");
  for (std::size_t k = 0; k < SnapshotTracker::WIDTH; ++k) {
    const auto a = snts.m_sm[0].m_buf[k];
    const auto b = snts.m_sm[1].m_buf[k];
    if (a != b) {
      std::println(stderr, "  cell {} (index {}): node0={:02x} node1={:02x}", k,
                   k, static_cast<unsigned>(a), static_cast<unsigned>(b));
    }
  }

  std::println("\n=== soak summary (seed={}) ===", seed);
  std::println("submitted:      {}", submitted);
  std::println("leader changes: {}", leader_changes);
  for (std::size_t idx : NODE_INDICIES) {
    std::println("node {} applied {} entries", idx, cluster.applied_count(idx));
  }

  if (failures > 0) {
    std::println(stderr, "\nreplay this failure with: ./test_soak {} {}", seed,
                 duration_s);
  }

  return report("soak", failures);
}
