/**
 * Crash-restart with only (term, voted_for) persisted.
 *
 * - When a follower is restarted it must catch back up with the rest of the
 *   cluster.
 * - When a leader is restarted the (term, voted_for) will be lower than the
 *   current term. Make sure the old leader doesn't try and depose the new
 *   leader.
 */

#include "raft_harness.hpp"

namespace fog::raft::test {

using namespace std::chrono_literals;

namespace {
constexpr std::uint8_t DATA = 0xE0;
constexpr auto SLEEP = 5s;
constexpr auto LONG_SLEEP = 10s;
} // namespace

TEST_F(TestContext, test_restart_follower) {

  // Replicate a few entries so the restarted node has something to refill.
  for (int i = 0; i < 4; ++i) {
    m_cluster->submit(m_leader_id, {static_cast<std::byte>(DATA + i)});
  }
  EXPECT_TRUE(m_cluster->await_applied(4, SLEEP))
      << " entries did not replicate";

  const std::size_t target = m_cluster->applied_count(m_leader_id);

  // Restart a follower.
  std::size_t follower = (m_leader_id == 0) ? 1 : 0;

  // Save state befor restarting.
  Observed::Persisted saved_before;
  {
    std::scoped_lock lock(m_cluster->observed().m_persisted_mtx);
    saved_before = m_cluster->observed().m_persisted[follower];
  }
  std::println("=== follower {} persisted term={} voted_for={} ===", follower,
               saved_before.m_term, saved_before.m_voted_for);

  // Restart the follower
  m_cluster->restart(follower);

  // It must refill its log from the leader and reach the same applied count.
  EXPECT_TRUE(m_cluster->await_applied(target, LONG_SLEEP))
      << std::format("restarted follower {} did not refill its log "
                     "(applied {} of {})",
                     follower, m_cluster->applied_count(follower), target);

  // The persisted term must not have gone backwards.
  {
    std::scoped_lock lock(m_cluster->observed().m_persisted_mtx);
    const auto now = m_cluster->observed().m_persisted[follower];
    EXPECT_GE(now.m_term, saved_before.m_term)
        << std::format("node {} term went backwards after restart ({} -> {})",
                       follower, saved_before.m_term, now.m_term);
  }
}

TEST_F(TestContext, test_restart_leader) {
  // A restarted leader should come back as a follower with an empty log.

  // Replicate a few entries so the restarted node has something to refill.
  for (int i = 0; i < 4; ++i) {
    m_cluster->submit(m_leader_id, {static_cast<std::byte>(DATA + i)});
  }
  EXPECT_TRUE(m_cluster->await_applied(4, SLEEP))
      << " entries did not replicate";

  const std::size_t before_restart = m_cluster->applied_count(m_leader_id);
  std::println("=== restarting leader {} (at {} entries) ===", m_leader_id,
               before_restart);
  m_cluster->restart(m_leader_id);

  const auto next_leader = m_cluster->await_leader();
  ASSERT_TRUE(next_leader.has_value()) << "no leader after leader restart";

  std::println("=== leader after restart: node {} ===", next_leader.value());

  // When it comes back it shouldn't take over as the leader again.
  EXPECT_NE(next_leader.value(), m_leader_id)
      << std::format("restarted node {} regained leadership", m_leader_id);

  // Make sure we can apply new values.
  const std::size_t before = m_cluster->applied_count(*next_leader);
  m_cluster->submit(next_leader.value(), {static_cast<std::byte>(DATA)});
  std::this_thread::sleep_for(SLEEP);
  EXPECT_GT(m_cluster->applied_count(*next_leader), before)
      << "no progress after leader restart";

  m_cluster->shutdown();
  EXPECT_EQ(check_invariants(m_cluster->observed()), 0);

  print_applied();
}

} // namespace fog::raft::test
