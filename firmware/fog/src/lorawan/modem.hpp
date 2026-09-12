#pragma once

#include "common/logging.hpp"
#include "fmt/core.h"
#include <charconv>
#include <cstdint>
#include <span>
#include <string_view>
#include <zephyr/modem/backend/uart.h>
#include <zephyr/modem/chat.h>
#include <zephyr/modem/pipe.h>


namespace fog::lora {

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
 * @brief Callback forward declarations for for modem_chat.
 */
namespace detail {
/**
 * @brief Handle an error when transmitting a command.
 * @param [in] chat The modem.
 * @param [in] argv The args.
 * @param [in] argc The number of args.
 * @param [in] user_data User data.
 */
void on_e5_at_error(struct modem_chat *chat, char **argv, std::uint16_t argc,
                    void *user_data);

/**
 * @brief Handle the not joined message.
 * @param [in] chat The modem.
 * @param [in] argv The args.
 * @param [in] argc The number of args.
 * @param [in] user_data User data.
 */
void on_e5_not_joined(struct modem_chat *chat, char **argv, std::uint16_t argc,
                      void *user_data);

/**
 * @brief Handle a downlink from the modem.
 * @param [in] chat The modem.
 * @param [in] argv The args.
 * @param [in] argc The number of args.
 * @param [in] user_data User data.
 */
void on_e5_downlink(struct modem_chat *chat, char **argv, std::uint16_t argc,
                    void *user_data);
} // namespace detail

/**
 * @brief Error tokens to look for when an AT command is in flight, finding any
 * of these tokens causes the command to be aborted.
 */
MODEM_CHAT_MATCH_DEFINE(e5_at_error_match, "AT_ERROR", "",
                        detail::on_e5_at_error);
MODEM_CHAT_MATCH_DEFINE(e5_at_param_error_match, "AT_PARAM_ERROR", "",
                        detail::on_e5_at_error);
MODEM_CHAT_MATCH_DEFINE(e5_not_joined_match, "Please join network first", "",
                        detail::on_e5_not_joined);
MODEM_CHAT_MATCHES_DEFINE(e5_abort_matches, e5_at_error_match,
                          e5_at_param_error_match, e5_not_joined_match);

/**
 * @brief Define to handle a downlink commands, these are setup at unsolicited
 * response as it doesn't end a message but it is part of the message we want to
 * listen to.
 */
MODEM_CHAT_MATCH_DEFINE(e5_downlink_match, "+MSGHEX: RX: \"", "\"",
                        detail::on_e5_downlink);
MODEM_CHAT_MATCHES_DEFINE(e5_unsol_matches, e5_downlink_match);

/**
 * @brief Controls the sending of command over UART to the E5 mini.
 */
class E5Modem {

  static constexpr std::size_t UART_RX_BUF_SIZE = 256;
  static constexpr std::size_t UART_TX_BUF_SIZE = 300;
  static constexpr std::size_t CHAT_RX_BUF_SIZE = 256;
  static constexpr std::size_t CHAT_ARGV_SIZE = 16;
  static constexpr std::size_t REQUEST_BUF_SIZE = 300;
  static constexpr std::size_t MATCH_BUF_SIZE = 64;

public:
  /**
   * @brief Constructor
   * @param port Interface to write and readline functions.
   */
  explicit E5Modem(const device *const uart) : m_uart{uart} {}

  bool init() {

    if (!device_is_ready(m_uart)) {
      return false;
    }

    // Setup UART.
    const struct modem_backend_uart_config backend_cfg = {
        .uart = m_uart,
        .receive_buf = m_uart_rx_buf.data(),
        .receive_buf_size = m_uart_rx_buf.size(),
        .transmit_buf = m_uart_tx_buf.data(),
        .transmit_buf_size = m_uart_tx_buf.size(),
    };

    m_pipe = modem_backend_uart_init(&m_uart_backend, &backend_cfg);
    if (m_pipe == nullptr) {
      return false;
    }

    // Configure the modem chat.
    const struct modem_chat_config chat_cfg = {
        .user_data = this,
        .receive_buf = m_chat_rx_buf.data(),
        .receive_buf_size = static_cast<std::uint16_t>(m_chat_rx_buf.size()),
        .delimiter = m_delimiter.data(),
        .delimiter_size = static_cast<std::uint8_t>(m_delimiter.size()),
        .filter = nullptr,
        .filter_size = 0,
        .argv = m_argv.data(),
        .argv_size = static_cast<std::uint16_t>(m_argv.size()),
        .unsol_matches = e5_unsol_matches,
        .unsol_matches_size = ARRAY_SIZE(e5_unsol_matches),
    };
    if (modem_chat_init(&m_chat, &chat_cfg) < 0) {
      return false;
    }

    init_script();

    if (modem_pipe_open(m_pipe, K_SECONDS(10)) < 0) {
      return false;
    }
    return modem_chat_attach(&m_chat, m_pipe) == 0;
  }

  /**
   * @brief Send `cmd`, read lines until `expected` matches, an error token
   * appears, or `timeout_ms` elapses. Any downlink in an RX: "<hex>" line is
   * decoded into `dl` (if non-empty). The decoded length is returned via
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
                std::size_t *dl_len = nullptr) {

    if (dl_len != nullptr) {
      *dl_len = 0;
    }
    m_downlink = downlink;
    m_dl_len_out = dl_len;
    m_abort_kind = CmdResult::TIMEOUT;

    if (!copy_terminated(command, m_request_buf) ||
        !copy_terminated(expected, m_expected_buf)) {
      logging::err("E5Modem: command/response too long for scratch buffer.");
      return CmdResult::AT_ERROR;
    }

    modem_chat_script_chat_set_request(&m_script_chat, m_request_buf.data());
    modem_chat_script_chat_set_timeout(&m_script_chat,
                                       static_cast<std::uint16_t>(timeout_ms));
    modem_chat_match_set_match(&m_expected_match, m_expected_buf.data());

    // Give the overall script a little more headroom than the single step,
    // so the step's own timeout is always what actually fires.
    const auto script_timeout_s =
        static_cast<std::uint16_t>((timeout_ms / 1000) + 2);
    modem_chat_script_set_timeout(&m_script, script_timeout_s);

    const int ret = modem_chat_run_script(&m_chat, &m_script);
    if (ret == 0) {
      return CmdResult::MATCHED;
    }

    logging::wrn("E5Modem: cmd failed, rc={}", ret);
    return m_abort_kind;
  }

  /**
   * @brief Send a simple command and check that the result matches expected.
   * @param [in] command The AT command.
   * @param [in] expected The expected response.
   * @param [in] timeout_ms The timeout in miliseconds to wait for a response.
   * @return true If the command was successful.
   */
  [[nodiscard]] bool ok(std::string_view command, std::string_view expected,
                        std::uint32_t timeout_ms) {
    return cmd(command, expected, timeout_ms) == CmdResult::MATCHED;
  }

  /**
   * @brief Called when a command aborts.
   * @param [in] kind The abort type.
   */
  void handle_abort(const CmdResult kind) noexcept { m_abort_kind = kind; }

  /**
   * @brief Handle a downlink from the modem.
   * @param [in] argv
   * @param [in] argc
   */
  void handle_downlink(char **argv, std::uint16_t argc) {

    // Check the caller wants a downlink.
    if (m_downlink.empty()) {
      return;
    }

    // For each line.
    for (std::uint16_t i = 0; i < argc; ++i) {

      // Make sure the downlink data is even, if odd it isn't a valid number of
      // hex bytes.
      const std::string_view token{argv[i]};
      if (token.empty() || (token.size() % 2) != 0) {
        continue;
      }

      // Make sure the whole string is made up of hex characters
      const bool looks_hex = std::ranges::all_of(token, [](const char c) {
        return std::isxdigit(static_cast<unsigned char>(c)) != 0;
      });
      if (!looks_hex) {
        continue;
      }

      // Decode the downlink into the user provided downlink buffer.
      std::size_t decoded = 0;
      bool ok_decode = true;
      const auto dl_buf_size = m_downlink.size();
      const auto ts = token.size();

      // Skip two chars at a time for each byte.
      for (std::size_t j = 0; j + 1 < ts && decoded < dl_buf_size; j += 2) {

        std::uint8_t byte{};
        constexpr int BASE = 16;

        const auto *begin = token.data() + j;
        const auto *end = token.data() + j + 2;
        auto [ptr, ec] = std::from_chars(begin, end, byte, BASE);
        if (ec != std::errc{} || ptr != end) {
          ok_decode = false;
          break;
        }
        m_downlink[decoded++] = static_cast<std::byte>(byte);
      }

      if (ok_decode && decoded > 0) {
        if (m_dl_len_out != nullptr) {
          *m_dl_len_out = decoded;
        }
        return;
      }
    }
  }

private:
  /**
   * @brief Initialise the modem chat script once for every command.
   */
  void init_script() {
    modem_chat_match_init(&m_expected_match);
    modem_chat_match_set_separators(&m_expected_match, "");

    modem_chat_script_chat_init(&m_script_chat);
    modem_chat_script_chat_set_response_matches(&m_script_chat,
                                                &m_expected_match, 1);

    modem_chat_script_init(&m_script);
    modem_chat_script_set_name(&m_script, "e5_cmd");
    modem_chat_script_set_script_chats(&m_script, &m_script_chat, 1);
    modem_chat_script_set_abort_matches(&m_script, e5_abort_matches,
                                        ARRAY_SIZE(e5_abort_matches));
  }

  /**
   * @brief Copy src into the dst buffer and add a null terminator.
   * @param [in] src The source data.
   * @param [in] dst The destination.
   * @return false If null terminated src doesn't fit in dst.
   */
  static bool copy_terminated(std::string_view src, std::span<char> dst) {
    if (src.size() + 1 > dst.size()) {
      return false;
    }
    std::memcpy(dst.data(), src.data(), src.size());
    dst[src.size()] = '\0';
    return true;
  }

  /** @brief The hardware UART handle. */
  const device *m_uart;

  /** @brief UART backend for handling the messages. */
  modem_backend_uart m_uart_backend{};

  /** @brief RX DMA buffer for the backend. */
  std::array<std::uint8_t, UART_RX_BUF_SIZE> m_uart_rx_buf{};

  /** @brief TX DMA buffer for the backend. */
  std::array<std::uint8_t, UART_TX_BUF_SIZE> m_uart_tx_buf{};

  /** @brief Pipe for the modem. */
  modem_pipe *m_pipe{nullptr};

  /** @brief The chat instance. */
  modem_chat m_chat{};

  /*** @brief Chat RX buffer for bytes read from the pipe. */
  std::array<std::uint8_t, CHAT_RX_BUF_SIZE> m_chat_rx_buf{};

  /** @brief Chat argv buffer populated with matches. */
  std::array<std::uint8_t *, CHAT_ARGV_SIZE> m_argv{};

  /** @brief Delimiters for the chat. */
  std::array<std::uint8_t, 2> m_delimiter{'\r', '\n'};

  /** @brief The expected match for the current command. */
  modem_chat_match m_expected_match{};

  /** @brief A single request send and response. */
  modem_chat_script_chat m_script_chat{};

  /** @brief The current script which encompasses m_script_chat. */
  modem_chat_script m_script{};

  /** @brief The current chat request buffer. */
  std::array<char, REQUEST_BUF_SIZE> m_request_buf{};

  /** @brief The current chat expected buffer. */
  std::array<char, MATCH_BUF_SIZE> m_expected_buf{};

  /** @brief The downlink data buffer, provided by the caller. */
  std::span<std::byte> m_downlink;

  /** @brief The pointer to tell the caller the length of the downlink data. */
  std::size_t *m_dl_len_out{nullptr};

  /**
   * @brief Set by the abort match callback to say why a message was aborted.
   */
  CmdResult m_abort_kind{CmdResult::TIMEOUT};
};

namespace detail {

inline void on_e5_at_error(struct modem_chat * /*chat*/, char ** /*argv*/,
                           std::uint16_t /*argc*/, void *user_data) {
  static_cast<E5Modem *>(user_data)->handle_abort(CmdResult::AT_ERROR);
}

inline void on_e5_not_joined(struct modem_chat * /*chat*/, char ** /*argv*/,
                             std::uint16_t /*argc*/, void *user_data) {
  static_cast<E5Modem *>(user_data)->handle_abort(CmdResult::NOT_JOINED);
}

inline void on_e5_downlink(struct modem_chat * /*chat*/, char **argv,
                           std::uint16_t argc, void *user_data) {
  static_cast<E5Modem *>(user_data)->handle_downlink(argv, argc);
}

} // namespace detail

} // namespace fog::lora
