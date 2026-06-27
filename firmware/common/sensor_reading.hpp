#pragma once

#include <cstdint>
#include <glaze/glaze.hpp>

namespace common {
/**
 * @brief Follows IEC 61850-7-3
 * https://cdn.standards.iteh.ai/samples/14982/302a451ae92e4a9ea9dbf56d819c0b2a/IEC-61850-7-3-2010.pdf
 */

/** @brief IEC 61850-7-3 Section 6.2 Quality — validity values */
enum class IEC61850_Validity : std::uint8_t {
  VALIDITY_GOOD = 0,         // no abnormal condition detected
  VALIDITY_INVALID = 1,      // abnormal condition, value shall not be used
  VALIDITY_QUESTIONABLE = 2, // abnormal behaviour but could still be valid
};

/** @brief IEC 61850-7-3 Section 6.2.3 Detail quality flags (bitmask) */
enum class IEC61850_DetailQual : std::uint8_t {
  DETAIL_NONE = 0x00,
  DETAIL_OVERFLOW = 0x01,     // value beyond representable range
  DETAIL_OUT_OF_RANGE = 0x02, // value beyond predefined range
  DETAIL_FAILURE = 0x04,      // internal or external failure detected
  DETAIL_OLD_DATA = 0x08,     // value not updated in expected time
  DETAIL_OUTLIER = 0x10,      // Outlier in BZT.
};

struct SensorReading {
  std::uint16_t water_level;
  IEC61850_Validity validity;
  IEC61850_DetailQual detail;
};

struct SensorReadingWire {
  std::uint64_t eui;
  std::uint16_t lvl;
  std::uint8_t seq;
  IEC61850_Validity val;
  IEC61850_DetailQual det;
};

/** @brief The CoAP URI for sensor data. */
static constexpr auto *SENSOR_URI = "sensor";

/** @brief Sensor configuration URI. */
static constexpr auto *SENSOR_CONFIG_URI = "config";

} // namespace common

template <> struct glz::meta<common::SensorReadingWire> {
  using T = common::SensorReadingWire;
  // By using glz::array instead of glz::object, we bypass the string naming
  // engine entirely. This is perfect for pure binary layouts where names aren't
  // serialized anyway!
  static constexpr auto value =
      glz::array(&T::eui, &T::lvl, &T::seq, &T::val, &T::det);
};
