#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace common {
/**
 * @brief Follows IEC 61850-7-3
 * https://cdn.standards.iteh.ai/samples/14982/302a451ae92e4a9ea9dbf56d819c0b2a/IEC-61850-7-3-2010.pdf
 */

/**
 * @brief Define a default false typed template for enabling bitwise operation
 * on enum classes.
 * @tparam E
 */
template <typename E> struct enable_bitmask_operators : std::false_type {};

/**
 * @brief Enable bitwise OR operations on an enum class.
 * @param [in] lhs
 * @param [in] rhs
 * @return std::enable_if_t<enable_bitmask_operators<E>::value, E>
 */
template <typename E>
typename std::enable_if_t<enable_bitmask_operators<E>::value, E>
operator|(const E lhs, const E rhs) noexcept {
  using underlying = std::underlying_type_t<E>;
  return static_cast<E>(static_cast<underlying>(lhs) |
                        static_cast<underlying>(rhs));
}

/**
 * @brief Enable bitwise AND operations on an enum class.
 * @param [in] lhs
 * @param [in] rhs
 * @return std::enable_if_t<enable_bitmask_operators<E>::value, E>
 */
template <typename E>
typename std::enable_if_t<enable_bitmask_operators<E>::value, E>
operator&(const E lhs, const E rhs) noexcept {
  using underlying = std::underlying_type_t<E>;
  return static_cast<E>(static_cast<underlying>(lhs) &
                        static_cast<underlying>(rhs));
}

/** @brief IEC 61850-7-3 Section 6.2 Quality - validity values */
enum class IEC61850_Validity : std::uint8_t {
  VALIDITY_GOOD = 0,         // no abnormal condition detected
  VALIDITY_INVALID = 1,      // abnormal condition, value shall not be used
  VALIDITY_QUESTIONABLE = 2, // abnormal behaviour but could still be valid
};

/**
 * @brief To string overload for IEC61850_Validity.
 * @param [in] val The validity
 * @return constexpr const char*
 */
constexpr const char *to_string(const IEC61850_Validity val) {
  switch (val) {
  case IEC61850_Validity::VALIDITY_GOOD:
    return "VALIDITY_GOOD";
  case IEC61850_Validity::VALIDITY_INVALID:
    return "VALIDITY_INVALID";
  case IEC61850_Validity::VALIDITY_QUESTIONABLE:
    return "VALIDITY_QUESTIONABLE";
  }
  return "Unkown";
}

/** @brief IEC 61850-7-3 Section 6.2.3 Detail quality flags (bitmask) */
enum class IEC61850_DetailQual : std::uint8_t {
  DETAIL_NONE = 0x00,
  DETAIL_OVERFLOW = 0x01,     // value beyond representable range
  DETAIL_OUT_OF_RANGE = 0x02, // value beyond predefined range
  DETAIL_FAILURE = 0x04,      // internal or external failure detected
  DETAIL_OLD_DATA = 0x08,     // value not updated in expected time
  DETAIL_OUTLIER = 0x10,      // Outlier in BZT.
};

/**
 * @brief To string overload for IEC61850_DetailQual.
 * @param [in] val The validity
 * @return constexpr const char*
 */
constexpr const char *to_string(const IEC61850_DetailQual det) {
  switch (det) {

  case IEC61850_DetailQual::DETAIL_NONE:
    return "DETAIL_NONE";
  case IEC61850_DetailQual::DETAIL_OVERFLOW:
    return "DETAIL_OVERFLOW";
  case IEC61850_DetailQual::DETAIL_OUT_OF_RANGE:
    return "DETAIL_OUT_OF_RANGE";
  case IEC61850_DetailQual::DETAIL_FAILURE:
    return "DETAIL_FAILURE";
  case IEC61850_DetailQual::DETAIL_OLD_DATA:
    return "DETAIL_OLD_DATA";
  case IEC61850_DetailQual::DETAIL_OUTLIER:
    return "DETAIL_OUTLIER";
  }
  return "Unkown";
}

/**
 * @brief Enable bitwise operations on IEC61850_DetailQual.
 */
template <>
struct enable_bitmask_operators<IEC61850_DetailQual> : std::true_type {};

/**
 * @brief Internal to edge for just the water level and validity + qual.
 */
struct SensorReading {
  std::uint16_t m_water_level;
  IEC61850_Validity m_validity;
  IEC61850_DetailQual m_detail;
};

/**
 * @brief The sensor reading that is sent across Thread.
 */
struct SensorReadingWire {
  std::uint64_t m_eui;
  std::uint64_t m_timestamp;
  std::uint16_t m_water_level_mm;
  IEC61850_Validity m_validity;
  IEC61850_DetailQual m_detail;
  std::uint8_t m_seq;
};

/** @brief The CoAP URI for sensor data. */
static constexpr auto *SENSOR_URI = "sensor";

/** @brief Sensor configuration URI. */
static constexpr auto *SENSOR_CONFIG_URI = "config";

/** @brief The number of expected sensors in the system. */
constexpr std::size_t MAX_SENSORS = 3;

} // namespace common
