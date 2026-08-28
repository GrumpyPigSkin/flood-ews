#pragma once

#define FMT_HEADER_ONLY

#include "fmt/format.h"
#include "zephyr/logging/log_core.h"
#include <array>
#include <fmt/compile.h>
#include <fmt/core.h>
#include <fmt/ranges.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(backhaul);

/**
 * @brief Helpers wrap the logging functionality, this prevents clangd moaning
 * about macro issues.
 */

namespace logging {
namespace detail {

template <typename... Args>
void log_impl(fmt::string_view prefix, fmt::format_string<Args...> fmt_str,
              Args &&...args) {
  fmt::memory_buffer buf;
  fmt::format_to(std::back_inserter(buf), "{}", prefix);
  fmt::format_to(std::back_inserter(buf), fmt_str, std::forward<Args>(args)...);
  fmt::println("{}", fmt::string_view(buf.data(), buf.size()));
}

} // namespace detail

template <typename... Args>
void err(fmt::format_string<Args...> fmt_str, Args &&...args) {
  if constexpr (__log_level >= LOG_LEVEL_ERR) {
    detail::log_impl("ERR: ", fmt_str, std::forward<Args>(args)...);
  }
}

template <typename... Args>
void wrn(fmt::format_string<Args...> fmt_str, Args &&...args) {
  if constexpr (__log_level >= LOG_LEVEL_WRN) {
    detail::log_impl("WRN: ", fmt_str, std::forward<Args>(args)...);
  }
}

template <typename... Args>
void inf(fmt::format_string<Args...> fmt_str, Args &&...args) {
  if constexpr (__log_level >= LOG_LEVEL_INF) {
    detail::log_impl("INF: ", fmt_str, std::forward<Args>(args)...);
  }
}

template <typename... Args>
void dbg(fmt::format_string<Args...> fmt_str, Args &&...args) {
  if constexpr (__log_level >= LOG_LEVEL_DBG) {
    detail::log_impl("DBG: ", fmt_str, std::forward<Args>(args)...);
  }
}

} // namespace logging
