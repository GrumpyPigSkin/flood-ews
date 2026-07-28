/**
 * InstallSnapshot transfer, including multi-chunk reassembly.
 *
 * Binds a real snapshot state machine and forces a follower far enough behind
 * that the leader has compacted past its next_index, the only way to catch it
 * up is then InstallSnapshot. The state machine straddles SNAPSHOT_CHUNK, so a
 * transferred snapshot exercises multi-chunk reassembly.
 */

#include "raft_harness.hpp"
#include "raft_state_machine.hpp"
#include <gtest/gtest.h>

namespace fog::raft::test {

using namespace std::chrono_literals;

namespace {

constexpr std::uint8_t BYTE_MASK = 0xFF;
constexpr auto SHORT_SLEEP = 20ms;
constexpr auto BURST_SLEEP = 50ms;
constexpr auto SLEEP = 3s;
constexpr auto LONG_SLEEP = 10s;
constexpr auto BURST_SLEEP_INTERVAL = 10;

} // namespace

TEST_F(TestContext, test_install_snapshot) {
  SnapshotTrackers snts;
  snts.reset();

  make_new_cluster(FaultConfig{}, 1, snts.hook());

  std::size_t follower = (m_leader_id == 0) ? 1 : 0;
  follower = std::min(follower, NUM_NODES);

  // Isolate a follower, then write past SNAPSHOT_THRESHOLD
  m_cluster->kill(follower);
  std::println("=== isolated follower {}, writing past SNAPSHOT_THRESHOLD "
               "({}) ===",
               follower, TestConfig::SNAPSHOT_THRESHOLD);

  const std::size_t writes = TestConfig::SNAPSHOT_THRESHOLD * 3;
  for (std::size_t i = 0; i < writes; ++i) {
    m_cluster->submit(m_leader_id, {static_cast<std::byte>(i & BYTE_MASK)});
    if ((i % BURST_SLEEP_INTERVAL) == 0) {
      std::this_thread::sleep_for(BURST_SLEEP);
    }
  }
  std::this_thread::sleep_for(SLEEP);

  const std::size_t target = m_cluster->applied_count(m_leader_id);
  std::println("=== leader applied {} entries, {} snapshots taken ===", target,
               snts.total_saves());

  // Revive and require catch-up.
  m_cluster->revive(follower);
  bool caught_up = false;
  const auto deadline = std::chrono::steady_clock::now() + LONG_SLEEP;
  while (std::chrono::steady_clock::now() < deadline) {
    if (snts.m_sm[follower].digest() == snts.m_sm[m_leader_id].digest() &&
        snts.m_sm[follower].m_applied_count > 0) {
      caught_up = true;
      break;
    }
    std::this_thread::sleep_for(SHORT_SLEEP);
  }

  EXPECT_TRUE(caught_up) << std::format(
      "follower {} did not catch up (applied {} of {}). ", follower,
      m_cluster->applied_count(follower), target);

  m_cluster->shutdown();

  EXPECT_EQ(check_invariants(m_cluster->observed()), 0);

  // Snapshot path reporting
  for (const auto idx : NODE_INDICIES) {
    std::println("node {} applied {} entries, {} saves, {} loads", idx,
                 m_cluster->applied_count(idx), snts.m_saves[idx].load(),
                 snts.m_loads[idx].load());
  }

  // Make sure the snapshot route was exercised.
  if (snts.total_saves() == 0) {
    ADD_FAILURE() << "m_snapshot_save was never called, compaction did "
                     "not fire, so InstallSnapshot was not exercised. Raise "
                     "the write count or lower SNAPSHOT_THRESHOLD to reach it.";
  } else if (snts.total_loads() == 0) {
    ADD_FAILURE() << std::format(
        "snapshots were taken ({}) but none transferred the follower, "
        "caught up via AppendEntries instead. Isolate it for longer to force "
        "InstallSnapshot.",
        snts.total_saves());
  } else {
    std::println("=== InstallSnapshot exercised: {} saves, {} loads ===",
                 snts.total_saves(), snts.total_loads());
  }

  // SnapshotTrackers state machines must agree. Compares applied STATE, not
  // just the apply log: catches a snapshot that transfers successfully but
  // reconstructs the wrong state.
  if (caught_up) {
    const std::uint64_t reference = snts.m_sm[m_leader_id].digest();
    for (const auto idx : NODE_INDICIES) {
      EXPECT_EQ(snts.m_sm[idx].digest(), reference)
          << std::format("node {} state machine diverged from node {} "
                         "(digest {:#x} vs {:#x})",
                         idx, m_leader_id, snts.m_sm[idx].digest(), reference);
    }
  }
}

} // namespace fog::raft::test
