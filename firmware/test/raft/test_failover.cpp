/**
 * Leader loss, failover, revival, and quorum loss.
 *
 * Checks:
 *  - a leader is elected from cold
 *  - killing the leader produces exactly one new leader
 *  - the new leader accepts writes
 *  - the revived node rejoins as a follower, not a second leader
 *  - with two of three nodes down, no leader emerges and progress stops
 */

#include <algorithm>

#include "raft_harness.hpp"

namespace fog::raft::test {

using namespace std::chrono_literals;

namespace {
constexpr auto SHORT_TIMEOUT = 2s;
constexpr auto TIMEOUT = 5s;
constexpr std::uint8_t PREFIX = 0x10;
constexpr std::uint8_t A_ENTRIES = 0xAA;
} // namespace

TEST_F(TestContext, test_leader_and_quorum_loss_and_revival) {

  // Step 1: Replicate some entries to start with.
  for (int i = 0; i < 4; ++i) {
    m_cluster->submit(m_leader_id, {static_cast<std::byte>(PREFIX + i)});
  }

  EXPECT_TRUE(m_cluster->await_applied(4, TIMEOUT))
      << "initial entries did not replicate";

  // Step 2: kill the leader.
  const std::size_t killed = m_leader_id;
  m_cluster->kill(killed);

  // Get the new leader.
  const auto next = m_cluster->await_leader();
  ASSERT_TRUE(next.has_value()) << "no failover after leader loss";

  // Make sure the leader changed.
  EXPECT_NE(next.value(), killed)
      << "partitioned node still reported as leader";

  std::println("=== New leader: node {} ===", next.value());

  // Write to the new leader..
  const std::size_t before = m_cluster->applied_count(*next);
  for (int i = 0; i < 3; ++i) {
    m_cluster->submit(*next, {static_cast<std::byte>(A_ENTRIES + i)});
  }
  std::this_thread::sleep_for(SHORT_TIMEOUT);
  EXPECT_GT(m_cluster->applied_count(*next), before)
      << "new leader made no progress";

  // Step 3: revive the old leader. This leader should not of incremented it's
  // term and should not campaign to become leader again.
  m_cluster->revive(killed);
  std::this_thread::sleep_for(SHORT_TIMEOUT);

  const auto settled = m_cluster->await_leader();
  ASSERT_TRUE(settled.has_value()) << "no single stable leader after revive";

  // Make sure the revived not hasn't taken back over as a leader.
  std::println("=== settled leader: node {} ===", *settled);
  EXPECT_NE(settled.value(), killed)
      << std::format("revived node {} won the post-revive election", killed);

  // Step 4: quorum loss.
  if (settled) {
    std::size_t second = (settled.value() == 0) ? 1 : 0;
    second = std::min(second, NUM_NODES);

    // Kill the majority of nodes.
    m_cluster->kill(settled.value());
    m_cluster->kill(second);

    // Let the survivor time out and and check it steps down.
    std::this_thread::sleep_for(SHORT_TIMEOUT);
    EXPECT_TRUE(m_cluster->assert_no_leader(SHORT_TIMEOUT))
        << "Should stall with no quorum.";
  }

  m_cluster->shutdown();
  ASSERT_EQ(check_invariants(m_cluster->observed()), 0);

  print_applied();
}

} // namespace fog::raft::test
