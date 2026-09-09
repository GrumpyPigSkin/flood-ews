#pragma once

#include "common/sensor_reading.hpp"
#include <array>
#include <cstdint>

namespace fog::batch {

/**
 * @brief A batch of readings a long with whether or not the alert is active.
 */
struct SensorBatch {
  /** @brief The recently received entries. */
  std::array<common::SensorReadingWire, common::MAX_SENSORS> m_entries;

  /** @brief The number of entries received. */
  std::uint8_t m_count;

  /** @brief True if an alert is active. */
  bool m_alert_active;
};

} // namespace fog::batch
