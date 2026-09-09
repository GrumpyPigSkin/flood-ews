#pragma once

#include <common/global_config.h>
#include <cstdint>

namespace edge::config {

constexpr std::uint32_t SLEEP_MS = common::DEFAULT_TIMEOUT_MS;
constexpr std::uint32_t SENSOR_WARMUP_MS = 100;
constexpr std::uint32_t SENSOR_TIMEOUT_MS = 1000;
constexpr std::uint32_t GROUND_DIST_MM = 2000;

} // namespace edge::config
