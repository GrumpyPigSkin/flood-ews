/**
 * Force conflicting logs, then check the loser truncates.
 *
 * 1. Leader A appends entries but is partitioned before they replicate.
 * 2. Survivors elect B, which appends *different* entries at the same indices.
 * 3. A is revived. Its uncommitted entries conflict with B's committed log, so
 *    A must truncate and adopt B's entries.
 */

#include "raft_harness.hpp"
#include <gtest/gtest.h>

namespace fog::raft::test {

using namespace std::chrono_literals;

namespace {
constexpr auto TIMEOUT = 5s;
constexpr auto SHORT_TIMEOUT = 300ms;
constexpr std::uint8_t PREFIX = 0x10;
constexpr std::uint8_t A_ENTRIES = 0xAA;
constexpr std::uint8_t B_ENTRIES = 0xBB;
} // namespace

TEST_F(TestContext, test_diverging_logs) {

  const std::size_t node_a = m_leader_id;

  // Commit a common prefix both sides will agree on.
  for (int i = 0; i < 2; ++i) {
    m_cluster->submit(node_a, {static_cast<std::byte>(PREFIX + i)});
  }

  // Make sure it was applied.
  EXPECT_TRUE(m_cluster->await_applied(2, TIMEOUT))
      << "common prefix did not replicate";
  const std::size_t prefix_len = m_cluster->applied_count(node_a);
  std::println("=== common prefix: {} entries ===", prefix_len);

  // Step 1: Append entries into A's log that go no where.
  // Mute send first so the entries reach A's log but no peer's.
  m_cluster->mute_send(node_a);
  for (int i = 0; i < 3; ++i) {
    m_cluster->submit(node_a,
                      {std::byte{A_ENTRIES}, static_cast<std::byte>(i)});
  }
  std::this_thread::sleep_for(SHORT_TIMEOUT);

  // Now cut receive too, so A cannot learn it has been deposed.
  m_cluster->mute_recv(node_a);
  std::println("=== node {} fully isolated with an uncommitted tail ===",
               node_a);

  // Step 2: Elect B and write different entries.
  const auto next = m_cluster->await_leader();
  ASSERT_TRUE(next.has_value()) << " survivors did not elect a new leader";

  const std::size_t node_b = next.value();
  std::println("=== leader B: node {} ===", node_b);

  // Append new entries.
  for (int i = 0; i < 3; ++i) {
    m_cluster->submit(node_b,
                      {std::byte{B_ENTRIES}, static_cast<std::byte>(i)});
  }
  std::this_thread::sleep_for(TIMEOUT);

  const std::size_t b_applied = m_cluster->applied_count(node_b);
  std::println("=== node {} applied {} entries ===", node_b, b_applied);
  EXPECT_GT(b_applied, prefix_len) << "leader B made no progress";

  // A's apply log must NOT contain the 0xAA entries as they were never
  // committed.
  {
    std::scoped_lock lock(m_cluster->observed().m_applied_mtx);
    const auto &a_log = m_cluster->observed().m_applied[node_a];
    EXPECT_LE(a_log.size(), prefix_len) << std::format(
        "node {} applied {} entries while isolated", node_a, a_log.size());
  }

  // Step 3: revive A and require convergence.
  m_cluster->revive(node_a);
  std::println("=== revived node {}, expecting truncation ===", node_a);

  const bool converged =
      m_cluster->await_applied(b_applied, std::chrono::seconds(15));
  EXPECT_TRUE(converged) << std::format("node {} did not catch up after revive",
                                        node_a);

  m_cluster->shutdown();

  // A and B must agree at every shared position.
  EXPECT_EQ(check_invariants(m_cluster->observed()), 0);

  print_applied();
}

} // namespace fog::raft::test
