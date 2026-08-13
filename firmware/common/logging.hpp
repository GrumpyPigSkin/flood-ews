#pragma once

#define FMT_HEADER_ONLY

#include "fmt/format.h"
#include "zephyr/logging/log_core.h"
#include <fmt/compile.h>
#include <fmt/core.h>
#include <fmt/ranges.h>
#include <stdio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(backhaul);

/**
 * @brief Helpers wrap the logging functionality, this prevents clangd moaning
 * about macro issues.
 */

namespace logging {

template <typename... Args>
void err(fmt::format_string<Args...> fmt_str, Args &&...args) {
#if defined(CONFIG_LOG)
  if (__log_level >= LOG_LEVEL_ERR) {
    fmt::memory_buffer buf;
    fmt::format_to(std::back_inserter(buf), "ERR: ");
    fmt::format_to(std::back_inserter(buf), fmt_str,
                   std::forward<Args>(args)...);
    fmt::println("{}", fmt::string_view(buf.data(), buf.size()));
  }
#endif
}

template <typename... Args>
void wrn(fmt::format_string<Args...> fmt_str, Args &&...args) {
#if defined(CONFIG_LOG)
  if (__log_level >= LOG_LEVEL_ERR) {
    fmt::memory_buffer buf;
    fmt::format_to(std::back_inserter(buf), "WRN: ");
    fmt::format_to(std::back_inserter(buf), fmt_str,
                   std::forward<Args>(args)...);
    fmt::println("{}", fmt::string_view(buf.data(), buf.size()));
  }
#endif
}

template <typename... Args>
void inf(fmt::format_string<Args...> fmt_str, Args &&...args) {
#if defined(CONFIG_LOG)
  if (__log_level >= LOG_LEVEL_ERR) {
    fmt::memory_buffer buf;
    fmt::format_to(std::back_inserter(buf), "INF: ");
    fmt::format_to(std::back_inserter(buf), fmt_str,
                   std::forward<Args>(args)...);
    fmt::println("{}", fmt::string_view(buf.data(), buf.size()));
  }
#endif
}

template <typename... Args>
void dbg(fmt::format_string<Args...> fmt_str, Args &&...args) {
#if defined(CONFIG_LOG)
  if (__log_level >= LOG_LEVEL_DBG) {
    fmt::memory_buffer buf;
    fmt::format_to(std::back_inserter(buf), "DBG: ");
    fmt::format_to(std::back_inserter(buf), fmt_str,
                   std::forward<Args>(args)...);
    fmt::println("{}", fmt::string_view(buf.data(), buf.size()));
  }
#endif
}

} // namespace logging
