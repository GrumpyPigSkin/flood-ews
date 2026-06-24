#pragma once

#include "common/sensor_reading.hpp"
#include "zephyr/sys/time_units.h"
#include <cstdint>

namespace edge::jsn {

constexpr std::uint16_t MIN_DISTANCE_MM = 250;  // 25 cm
constexpr std::uint16_t MAX_DISTANCE_MM = 4500; // 4.5 m

/**
 * @brief Speed-of-sound conversion from the datasheet:
 * distance_mm = us * 10 / 58
 * @param [in] pulse_width_us the pulse width is microseconds.
 * @return constexpr std::uint32_t
 */
[[nodiscard]] constexpr std::uint32_t
distance_mm_from_us(const std::uint32_t pulse_width_us) noexcept {
  const std::uint32_t MULTIPLIER = 10;
  const std::uint32_t COEFF = 58;
  return pulse_width_us * MULTIPLIER / COEFF;
}

/**
 * @brief State of the current reading, also used to determine validity.
 */
enum class ReadingState : std::uint8_t {
  IDLE,         // no measurement in flight / not started
  IN_FLIGHT,    // triggered, waiting on echo edges
  VALID,        // got a falling edge, distance within range
  OUT_OF_RANGE, // got a reading but outside [min, max]
  TIMEOUT,      // never saw a falling edge
};

/**
 * @brief Helper function to derive the validity and quality of a reading before
 * we pass it upstream.
 * @param [in] is_ready Is the reading valid, should be false if it came from a
 * timeout.
 * @param [in] dist The distance measured.
 * @param [in] ground_dist The distance to the ground to figure out level above
 * ground.
 * @return constexpr common::SensorReading
 */
[[nodiscard]] constexpr common::SensorReading
calculate_reading(const bool is_valid, const std::uint16_t dist,
                  const std::uint16_t ground_dist) noexcept {

  using common::IEC61850_DetailQual;
  using common::IEC61850_Validity;
  using common::SensorReading;

  SensorReading result = {.water_level = 0,
                          .validity = IEC61850_Validity::VALIDITY_INVALID,
                          .detail = IEC61850_DetailQual::DETAIL_FAILURE};

  if (!is_valid) {
    return result;
  }

  if (dist > ground_dist) {
    result.validity = IEC61850_Validity::VALIDITY_QUESTIONABLE;
    result.detail = IEC61850_DetailQual::DETAIL_OUT_OF_RANGE;
    result.water_level = 0;
  } else if (dist < MIN_DISTANCE_MM) {
    result.validity = IEC61850_Validity::VALIDITY_QUESTIONABLE;
    result.detail = IEC61850_DetailQual::DETAIL_OVERFLOW;
    result.water_level = ground_dist;
  } else {
    result.water_level = ground_dist - dist;
    result.validity = IEC61850_Validity::VALIDITY_GOOD;
    result.detail = IEC61850_DetailQual::DETAIL_NONE;
  }

  return result;
}

/**
 * @brief Helper function to wrap k_cyc_to_us_near32 to contain clangd warnings.
 * @param value
 * @return constexpr auto
 */
[[nodiscard]] constexpr auto cyc_to_us_near32(const std::uint32_t value) {
  return k_cyc_to_us_near32(value);
}

} // namespace edge::jsn
