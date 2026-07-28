/**
 * Two cases that a symmetric dead/alive cannot test for:
 *
 * - A follower that receives heartbeats but cannot reply. The leader sees it
 *   as unreachable and its match_index never advances, but the follower never
 *   times out, so it does not campaign. The cluster should keep working on
 *   the remaining majority.
 *
 * - A leader that can send but not receive. It keeps broadcasting heartbeats,
 *   so followers stay quiet and never elect a replacement, but it can never
 *   learn anything is replicated, so it can never commit. After some time
 *   without a reply the leader should step down, but the new cluster needs to
 *   protect against the stepped down leader campaigning to become the leader
 *   again.
 */

#include "raft_harness.hpp"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <format>
#include <gtest/gtest.h>
#include <memory>
#include <ranges>

namespace fog::raft::test {

using namespace std::chrono_literals;

namespace {
constexpr auto TIMEOUT = 5s;
constexpr std::uint8_t DATA = 0xC0;
} // namespace

TEST_F(TestContext, test_muted_follower_does_not_stall_cluster) {

  // Pick any node that is not the leader.
  const std::size_t follower = *std::ranges::find_if(
      NODE_INDICIES, [this](auto idx) { return idx != m_leader_id; });

  // Mute the send.
  m_cluster->mute_send(follower);

  // Ensure the leader can still commit new entries.
  const std::size_t before = m_cluster->applied_count(m_leader_id);

  submit_batch(std::byte{DATA});
  std::this_thread::sleep_for(TIMEOUT);

  EXPECT_GT(m_cluster->applied_count(m_leader_id), before)
      << "cluster stalled with only one muted follower";

  // Make sure the muted follower does not try to become a leader.
  EXPECT_NE(m_cluster->role(follower), State::LEADER)
      << std::format("mute follower {} became leader", follower);

  ASSERT_EQ(finish_and_check(), 0);
}

TEST_F(TestContext, test_deaf_leader_cannot_commit_new_entries) {

  // Mute the leaders receiver.
  m_cluster->mute_recv(m_leader_id);
  const std::size_t before = m_cluster->applied_count(m_leader_id);

  // Submit some new data.
  submit_batch(std::byte{DATA});
  std::this_thread::sleep_for(TIMEOUT);

  // Make sure that the leader did not commit the new data.
  const std::size_t after = m_cluster->applied_count(m_leader_id);
  EXPECT_LE(after, before) << std::format(
      "deaf leader committed {} entries without hearing a single response.",
      after - before);

  // Make sure the old leader steps down from it's role after no hearing from
  // other nodes.
  const State role = m_cluster->role(m_leader_id);
  EXPECT_NE(role, State::LEADER) << std::format(
      "node {} still claims leadership without hearing from any peer.",
      m_leader_id);

  ASSERT_EQ(finish_and_check(), 0);
}

} // namespace fog::raft::test
