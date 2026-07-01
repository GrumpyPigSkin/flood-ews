#pragma once

#include "common/logging.hpp"
#include "fmt/core.h"
#include "lorawan/protocol.hpp"
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string_view>

namespace fog::lora {

/**
 * @brief Uart port acts as a shim between the physical UART handling in
 * uart_e5.hpp and the rest of the application.
 * This split allows for testing.
 */
struct UartPort {
  /** @brief Write over UART. */
  std::function<void(std::string_view)> m_write;

  /** @brief Read a line from UART. */
  std::function<std::optional<std::string_view>(std::uint32_t timeout_ms)>
      m_read_line;
};

/**
 * @brief Timeouts for certain command types.
 */
struct Timeouts {
  static constexpr std::uint32_t SHORT_MS = 2000;
  static constexpr std::uint32_t DEFAULT_MS = 10000;
  static constexpr std::uint32_t LONG_MS = 30000;
};

/**
 * @brief Outcome of an AT exchange.
 */
enum class CmdResult : std::uint8_t {
  MATCHED,    // expected substring seen, no error
  TIMEOUT,    // deadline hit before a match
  AT_ERROR,   // AT_ERROR / AT_PARAM_ERROR token
  NOT_JOINED, // "Please join network first"
};

/**
 * @brief Controls the sending of command over UART to the E5 mini.
 */
class E5Modem {
  static constexpr int MAX_LINES = 64;

public:
  /**
   * @brief Constructor
   * @param port Interface to write and readline functions.
   */
  explicit E5Modem(UartPort port) : m_port{std::move(port)} {}

  /**
   * @brief Send `cmd`, read lines until `expected` matches, an error token
   * appears, or `timeout_ms` elapses. Any downlink in an RX: "<hex>" line is
   * decoded into `dl` (if non-empty); the decoded length is returned via
   * `dl_len`.
   * @param [in] command The AT command.
   * @param [in] expected The expected response.
   * @param [in] timeout_ms The timeout in miliseconds to wait for a response.
   * @param [in] downlink Buffer to store downlink data.
   * @param [in] dl_len The length of data read into downlink.
   * @return CmdResult
   */
  CmdResult cmd(std::string_view command, std::string_view expected,
                std::uint32_t timeout_ms, std::span<std::byte> downlink = {},
                std::size_t *dl_len = nullptr) const {

    m_port.m_write(command);
    if (dl_len != nullptr) {
      *dl_len = 0;
    }

    // Read up to 64 lines to get the result we want, or timeout. This somewhat
    // expects the UART to reply quickly, if we had an incorrect line at lets
    // say 4.9 seconds in the 5 second timeout, this would reset the timer.
    for (int guard = 0; guard < MAX_LINES; ++guard) {
      const auto line = m_port.m_read_line(timeout_ms);
      if (!line) {
        return CmdResult::TIMEOUT;
      }

      if ((dl_len != nullptr) && !downlink.empty()) {
        if (const std::size_t itr = parse_downlink(*line, downlink); itr > 0) {
          *dl_len = itr;
        }
      }

      if (line->contains("AT_ERROR") || line->contains("AT_PARAM_ERROR")) {
        return CmdResult::AT_ERROR;
      }

      if (line->contains("Please join network first")) {
        return CmdResult::NOT_JOINED;
      }

      if (line->contains(expected)) {
        constexpr std::uint32_t DRAIN_MS = 150;
        while (m_port.m_read_line(DRAIN_MS)) { /* discard */
        }
        return CmdResult::MATCHED;
      }
    }
    return CmdResult::TIMEOUT;
  }

  /**
   * @brief Send a simple command and check that the result matches expected.
   * @param [in] command The AT command.
   * @param [in] expected The expected response.
   * @param [in] timeout_ms The timeout in miliseconds to wait for a response.
   * @return true If the command was successful.
   */
  [[nodiscard]] bool ok(std::string_view command, std::string_view expected,
                        std::uint32_t timeout_ms) const {
    return cmd(command, expected, timeout_ms) == CmdResult::MATCHED;
  }

private:
  /** @brief Interface to read and write. */
  UartPort m_port;
};

} // namespace fog::lora
