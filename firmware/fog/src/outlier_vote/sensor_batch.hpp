#pragma once

#include "common/sensor_reading.hpp"
#include <array>
#include <cstdint>

namespace fog::batch {

struct SensorBatch {
  std::array<common::SensorReadingWire, common::MAX_SENSORS> m_entries;
  std::uint8_t m_count;
  bool m_alert_active;
};

} // namespace fog::batch
