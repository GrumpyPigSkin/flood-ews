#pragma once

#include <array>
#include <cstddef>
#include <openthread/instance.h>
#include <openthread/link.h>
#include <zephyr/sys/byteorder.h>

namespace common {

constexpr std::size_t EUI64_LEN = 8;
using Eui64Arr = std::array<std::uint8_t, EUI64_LEN>;

/**
 * @brief Get the eui64
 * @return otExtAddress
 */
inline otExtAddress get_eui64() {
  otInstance *const ot_inst = openthread_get_default_instance();
  otExtAddress eui64;
  otLinkGetFactoryAssignedIeeeEui64(ot_inst, &eui64);
  return eui64;
}

/**
 * @brief Get the eui64 as a uint64_t
 * @return std::uint64_t
 */
inline std::uint64_t get_eui64_as_uint64() {
  const auto eui = get_eui64();
  return sys_get_be64(eui.m8);
}

/**
 * @brief Get the eui64 as an std::array<std::uint8_t, EUI64_LEN>;
 * @return std::array<std::uint8_t, EUI64_LEN>;
 */
inline Eui64Arr get_eui64_as_arr8() {
  const auto eui = get_eui64();
  Eui64Arr eui_out;
  std::memcpy(eui_out.data(), eui.m8, eui_out.size());
  return eui_out;
}

} // namespace common
