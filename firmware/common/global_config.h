/**
 * Global configuration variables used between edge and fog layer.
 */
#pragma once

#include <cstdint>

namespace common {

#ifdef CONFIG_ENABLE_FAULT_INJECTION
/** @brief Default timeout for steady state in seconds. */
constexpr std::uint64_t DEFAULT_TIMEOUT_S = 60;
#else
/** @brief Default timeout for steady state in seconds. */
constexpr std::uint64_t DEFAULT_TIMEOUT_S = 300;
#endif

/** @brief Default timeout for steady state in miliseconds. */
constexpr std::uint64_t DEFAULT_TIMEOUT_MS = DEFAULT_TIMEOUT_S * 1000;

/** @brief Alert timeout in seconds. */
constexpr std::uint64_t ALERT_TIMEOUT_S = 30;

/** @brief Alert timeout in miliseconds. */
constexpr std::uint64_t ALERT_TIMEOUT_MS = ALERT_TIMEOUT_S * 1000;

} // namespace common
