#pragma once

/**
 * Helper class to track snapshotting to test the snapshotting functionality.
 */

#include "raft_harness.hpp"
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <numeric>

namespace fog::raft::test {

struct SnapshotTracker {
  /** @brief > SNAPSHOT_CHUNK, < SNAPSHOT_MAX */
  static constexpr std::size_t WIDTH = 300;

  /** @brief Data buffer. */
  std::array<std::byte, WIDTH> m_buf{};

  /** @brief Number of times applied was called. */
  std::uint64_t m_applied_count{0};

  /** @brief Apply the entry. */
  void apply(const EntryT &entry) {
    ++m_applied_count;

    // Ignore noops.
    if (entry.m_data_len == 0) {
      return;
    }

    // Save the data.
    const auto slot = static_cast<std::size_t>(entry.m_index % WIDTH);
    m_buf[slot] = static_cast<std::byte>(entry.payload()[0]);
  }

  /** @brief Serialise into buf, negative if it does not fit. */
  [[nodiscard]] std::int32_t save(std::uint8_t *buf,
                                  const std::uint32_t len) const {
    const auto need =
        static_cast<std::uint32_t>(WIDTH + sizeof(m_applied_count));
    if (len < need) {
      return -1;
    }
    std::memcpy(buf, m_buf.data(), WIDTH);
    std::memcpy(buf + WIDTH, &m_applied_count, sizeof(m_applied_count));
    return static_cast<std::int32_t>(need);
  }

  /**
   * @brief Load into m_buf.
   */
  void load(const std::uint8_t *buf, const std::uint32_t len) {
    if (len < WIDTH + sizeof(m_applied_count)) {
      return;
    }
    std::memcpy(m_buf.data(), buf, WIDTH);
    std::memcpy(&m_applied_count, buf + WIDTH, sizeof(m_applied_count));
  }

  /** @brief Hash over m_buf. */
  [[nodiscard]] std::uint64_t digest() const { return payload_hash(m_buf); }
};

/**
 * @brief Per-node snapshot activity counters.
 */
struct SnapshotTrackers {
  std::array<SnapshotTracker, NUM_NODES> m_sm{};
  std::array<std::atomic<std::uint64_t>, NUM_NODES> m_saves{};
  std::array<std::atomic<std::uint64_t>, NUM_NODES> m_loads{};

  /**
   * @brief Reset the state.
   */
  void reset() {
    for (const auto idx : NODE_INDICIES) {
      m_sm[idx] = SnapshotTracker{};
      m_saves[idx].store(0);
      m_loads[idx].store(0);
    }
  }

  /** @brief Reset the given `node` */
  void reset(const std::size_t node) {
    m_sm[node] = SnapshotTracker{};
    m_saves[node].store(0);
    m_loads[node].store(0);
  }

  /**
   * @brief Get the total number of saves done.
   * @return std::uint64_t
   */
  [[nodiscard]] std::uint64_t total_saves() const {
    return std::accumulate(std::begin(m_saves), std::end(m_saves), 0);
  }

  /**
   * @brief Get the total number of loads.
   * @return std::uint64_t
   */
  [[nodiscard]] std::uint64_t total_loads() const {
    return std::accumulate(std::begin(m_loads), std::end(m_loads), 0);
  }

  /**
   * @brief A CallbackHook that binds this set of machines into a Cluster.
   * Chains onto the harness's own m_apply rather than replacing it, so the
   * invariant checks still see every apply.
   */
  [[nodiscard]] CallbackHook hook() {
    return [this](const std::size_t self, Callbacks<TestConfig> &cbs) {
      cbs.m_apply = [this, self,
                     // Save the original apply.
                     inner = cbs.m_apply](const EntryT &entry) {
        if (inner) {
          inner(entry);
        }
        m_sm[self].apply(entry);
      };
      cbs.m_snapshot_save = [this,
                             self](std::uint8_t *buf,
                                   const std::uint32_t cap) -> std::int32_t {
        m_saves[self].fetch_add(1, std::memory_order_relaxed);
        return m_sm[self].save(buf, cap);
      };
      cbs.m_snapshot_load = [this, self](const std::uint8_t *buf,
                                         const std::uint32_t len) {
        m_loads[self].fetch_add(1, std::memory_order_relaxed);
        m_sm[self].load(buf, len);
      };
    };
  }
};

} // namespace fog::raft::test
