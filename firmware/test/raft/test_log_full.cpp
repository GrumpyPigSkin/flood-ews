/**
 * Behaviour when the log ring approaches and exceeds capacity.
 *
 * LOG_CAPACITY is 64 and SNAPSHOT_THRESHOLD is 32. Submitting well past
 * capacity must result in the log compacting.
 */

#include "raft_harness.hpp"
#include "raft_state_machine.hpp"

namespace fog::raft::test {

using namespace std::chrono_literals;

namespace {

constexpr std::size_t BURST = 200; // Well past capacity.
constexpr std::uint8_t BYTE_MASK = 0xFF;
constexpr std::uint8_t BYTE_SHIFT = 8;
constexpr auto BURST_SLEEP = 50ms;
constexpr auto SLEEP = 3s;
constexpr auto BURST_SLEEP_INTERVAL = 10;

} // namespace

TEST_F(TestContext, test_log_full) {
  SnapshotTrackers snts;
  snts.reset();

  make_new_cluster(FaultConfig{}, 1, snts.hook());

  // Burst well past capacity
  std::println("=== submitting {} entries (LOG_CAPACITY={}, "
               "SNAPSHOT_THRESHOLD={}) ===",
               BURST, TestConfig::LOG_CAPACITY, TestConfig::SNAPSHOT_THRESHOLD);

  for (std::size_t i = 0; i < BURST; ++i) {
    m_cluster->submit(m_leader_id,
                      {static_cast<std::byte>(i & BYTE_MASK),
                       static_cast<std::byte>((i >> BYTE_SHIFT) & BYTE_MASK)});
    // Pace out the writes.
    if ((i % BURST_SLEEP_INTERVAL) == 0) {
      std::this_thread::sleep_for(BURST_SLEEP);
    }
  }

  std::this_thread::sleep_for(SLEEP);

  const std::size_t applied = m_cluster->applied_count(m_leader_id);

  // With snapshotting bound and SNAPSHOT_THRESHOLD < LOG_CAPACITY, the burst
  // must fully commit.
  EXPECT_GE(applied, BURST) << std::format(
      "only {} of {} entries committed compaction is not reclaiming log space",
      applied, BURST);

  // Make sure snapshotting was called.
  EXPECT_NE(snts.total_saves(), 0)
      << "no snapshot was ever taken despite exceeding SNAPSHOT_THRESHOLD";

  std::this_thread::sleep_for(SLEEP);

  const auto current = m_cluster->await_leader();
  ASSERT_TRUE(current) << "no leader after the burst";

  // The cluster must still accept new work.
  const std::size_t before = m_cluster->applied_count(*current);
  m_cluster->submit(*current, {std::byte{BYTE_MASK}});
  std::this_thread::sleep_for(SLEEP);
  const std::size_t after = m_cluster->applied_count(*current);

  EXPECT_GT(after, before) << std::format(
      "cluster not accepting new data after burst ({} applied before, {} "
      "after)",
      before, after);

  m_cluster->shutdown();

  // Check logs match across nodes.
  EXPECT_EQ(check_invariants(m_cluster->observed()), 0);

  print_applied();
}

} // namespace fog::raft::test
