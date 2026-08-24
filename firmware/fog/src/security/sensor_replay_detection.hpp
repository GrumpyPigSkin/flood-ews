#pragma once

#include "common/sensor_reading.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

namespace fog::security {

/**
 * @brief Replay protection ensures that the received message's sequence number
 * is higher than the previous. The sender must persist this sequence number.
 */
class SensorReplayDetection {
public:
  static constexpr std::size_t MAX_SENSORS = 8;
  static constexpr std::int64_t TIMEOUT_US = 1200'000'000; // 20 mins

  /** @brief Helper struct for table. */
  struct SensorReplayEntry {
    std::uint64_t m_eui;
    std::uint32_t m_last_sequence;
    bool m_is_active;
  };

  using ReplayArrT = std::array<SensorReplayEntry, MAX_SENSORS>;

  /**
   * @brief Check that we haven't seen this entry before.
   * @param [in] reading
   * @return true if the reading is fresh.
   */
  bool is_fresh(const common::SensorReadingWire &reading) {

    auto *const entry = find_or_create(reading.m_eui);

    // No entry available.
    if (entry == nullptr) {
      return false;
    }

    if (!entry->m_is_active) {
      // First time the sensor has been seen.
      entry->m_is_active = true;
      entry->m_last_sequence = reading.m_seq;
      return true;
    }

    // If this sequence is newer than the stored one, that is a win.
    if (entry->m_last_sequence < reading.m_seq) {
      entry->m_last_sequence = reading.m_seq;
      return true;
    }

    return false;
  }

private:
  /**
   * @brief Fine an existing entry
   * @param [in] eui The EUI to fetch..
   * @return SensorReplayEntry* nullptr if table is full.
   */
  SensorReplayEntry *find_or_create(const std::uint64_t eui) {
    for (auto &entry : m_entries) {
      if (entry.m_eui == eui) {
        return &entry;
      }
    }

    for (auto &entry : m_entries) {
      if (!entry.m_is_active) {
        entry.m_eui = eui;
        return &entry;
      }
    }

    return nullptr;
  }

  /** @brief The entry table. */
  ReplayArrT m_entries{};
};

} // namespace fog::security
