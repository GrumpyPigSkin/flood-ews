/**
 * Test that when a follower is isolated and new entries have been committed
 * that the follower catches back up when it is alive again.
 */

#include "raft_harness.hpp"
#include <algorithm>
#include <chrono>
#include <print>
#include <ranges>

namespace fog::raft::test {
using namespace std::chrono_literals;

namespace {

constexpr std::size_t LAG_ENTRIES = 24;
constexpr auto TIMEOUT = 5s;

} // namespace

TEST_F(TestContext, test_follower_catches_up) {

  // Pick a follower to fall behind.
  auto nodes = std::views::iota(std::size_t{0}, NUM_NODES);
  const std::size_t follower = *std::ranges::find_if(
      nodes, [this](auto idx) { return idx != m_leader_id; });

  // Establish a common prefix.
  m_cluster->submit(m_leader_id, {std::byte{0x01}});
  EXPECT_TRUE(m_cluster->await_applied(1, TIMEOUT))
      << "prefix did not replicate";

  // Isolate the follower and build up lag.
  m_cluster->kill(follower);
  std::println("=== isolated follower {}, writing {} entries ===", follower,
               LAG_ENTRIES);

  for (std::size_t i = 0; i < LAG_ENTRIES; ++i) {
    m_cluster->submit(m_leader_id, {static_cast<std::byte>(i)});
  }

  // The remaining nodes are still a majority, so these commit.
  std::this_thread::sleep_for(TIMEOUT);
  const std::size_t target = m_cluster->applied_count(m_leader_id);
  std::println("=== leader at {} applied entries ===", target);

  EXPECT_GE(target, LAG_ENTRIES) << std::format(
      "leader only applied {} of {} entries", target, LAG_ENTRIES);

  // Revive and measure the catch-up cost.
  m_cluster->revive(follower);

  const bool caught_up = m_cluster->await_applied(target, 20s);

  EXPECT_NE(caught_up, 0) << std::format("follower {} never caught up",
                                         follower);

  m_cluster->shutdown();

  // Make all nodes are caught up.
  EXPECT_EQ(check_invariants(m_cluster->observed()), 0);

  // Print out what was actually applied for debugging.
  print_applied();
}

} // namespace fog::raft::test
