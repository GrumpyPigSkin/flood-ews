#pragma once

#include "common/ot_utils.hpp"
#include "lorawan/modem.hpp"
#include "lorawan/protocol.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string_view>

namespace fog::lora {

/**
 * @brief Identity + keys for the OTAA join.
 */
struct JoinParams {
  std::array<std::uint8_t, common::EUI64_LEN> eui{};
  std::string_view app_key;
};

static constexpr std::size_t EUI_64_HEX_LEN = 17;

/**
 * @brief Format an 8-byte EUI as 16 uppercase hex chars.
 * @param [in] eui The EUI
 * @return std::array<char, 17>
 */
[[nodiscard]] constexpr std::array<char, EUI_64_HEX_LEN>
eui_hex(std::span<const std::uint8_t, common::EUI64_LEN> eui) noexcept {
  std::array<char, EUI_64_HEX_LEN> out{};
  (void)to_hex<std::uint8_t>(eui, out);
  out[EUI_64_HEX_LEN - 1] = '\0';
  return out;
}

constexpr std::string_view CHECK_AT_STR = "AT\r\n";
constexpr std::string_view CHECK_AT_RSP = "+AT: OK";

constexpr std::string_view SET_OTAA_STR = "AT+MODE=LWOTAA\r\n";
constexpr std::string_view SET_OTAA_RSP = "+MODE: LWOTAA";

constexpr std::string_view SET_APP_EUI_CMD = "AT+ID=AppEui,\"%s\"\r\n";
constexpr std::string_view SET_APP_EUI_RSP = "+ID: AppEui";

constexpr std::string_view SET_DEV_EUI_CMD = "AT+ID=DevEui,\"%s\"\r\n";
constexpr std::string_view SET_DEV_EUI_RSP = "+ID: DevEui";

constexpr std::string_view SET_APP_KEY_CMD = "AT+KEY=APPKEY,\"%s\"\r\n";
constexpr std::string_view SET_APP_KEY_RSP = "+KEY: APPKEY";

constexpr std::string_view SET_DATA_RATE_CMD = "AT+DR=DR5\r\n";
constexpr std::string_view SET_DATA_RATE_RSP = "+DR: DR5";

constexpr std::string_view SET_EU868_CMD = "AT+DR=EU868\r\n";
constexpr std::string_view SET_EU868_RSP = "+DR: EU868";

constexpr std::string_view JOIN_CMD = "AT+JOIN\r\n";
constexpr std::string_view JOIN_RSP = "+JOIN: Network joined";

constexpr std::string_view FORCE_JOIN_CMD = "AT+JOIN=FORCE\r\n";
constexpr std::string_view FORCE_JOIN_RSP = "+JOIN: Network joined";

constexpr std::string_view MSG_START_CMD = "AT+MSGHEX=\"";
constexpr std::string_view MSG_FMT_CMD = "%02X";
constexpr std::string_view MSG_END_CMD = "\"\r\n";
constexpr std::string_view MSG_PING_CMD = "AT+MSGHEX=\"BEEF\"\r\n";
constexpr std::string_view MSG_DONE_RSP = "+MSGHEX: Done";
constexpr std::string_view E5_TOK_NOT_JOINED = "Please join network first";

constexpr std::string_view E5_STATUS_CMD = "AT+NJS=?\r\n";
constexpr std::string_view E5_STATUS_JOINED = "+NJS: 1";

// Disable duty cycle for testing.
constexpr std::string_view E5_DUTY_CYCLE_OFF_CMD = "AT+LW=DC, OFF\r\n";
constexpr std::string_view E5_DUTY_CYCLE_OFF_RSP = "+LW: DC, OFF, 0";

constexpr std::string_view E5_JOIN_DUTY_CYCLE_OFF_CMD = "AT+LW=JDC, OFF\r\n";
constexpr std::string_view E5_JOIN_DUTY_CYCLE_OFF_RSP = "+LW: JDC, OFF";

constexpr std::string_view E5_DISABLE_AUTO_DATA_RATE_CMD = "AT+ADR=OFF\r\n";
constexpr std::string_view E5_DISABLE_AUTO_DATA_RATE_RSP = "+ADR: OFF";

constexpr std::string_view AT_PARAM_ERR = "AT_PARAM_ERROR";
constexpr std::string_view AT_ERR = "AT_ERROR";
/**
 * @brief Run the full OTAA bring-up: AT, OTAA mode, AppEui/DevEui, AppKey, DR,
 * region, then JOIN.
 * @param [in] modem The modem to send the command over.
 * @param [in] params The params for joining.
 * @param [inout] scratch is a caller-provided buffer for building parameterized
 * commands.
 * @return true if joined successfully.
 */
inline bool run_join(E5Modem &modem, const JoinParams &params,
                     std::span<char> scratch) {
  const auto eui = eui_hex(std::span<const std::uint8_t, 8>{params.eui});

  // Lambda to build a command + parameters
  auto fmt = [&](std::string_view fmt,
                 std::string_view arg) -> std::string_view {
    const int num_written =
        std::snprintf(scratch.data(), scratch.size(), fmt.data(), arg.data());
    return (num_written > 0)
               ? std::string_view{scratch.data(),
                                  static_cast<std::size_t>(num_written)}
               : std::string_view{};
  };

  struct E5Cmd {
    std::string_view m_cmd;
    std::string_view m_arg;
    std::string_view m_rsp;
    std::uint32_t m_timeout;
  };

  std::array setup_commands = {
      E5Cmd{CHECK_AT_STR, {}, CHECK_AT_RSP, Timeouts::SHORT_MS},
      E5Cmd{SET_OTAA_STR, {}, SET_OTAA_RSP, Timeouts::SHORT_MS},
      E5Cmd{SET_APP_EUI_CMD, std::string_view{eui.data(), eui.size()},
            SET_APP_EUI_RSP, Timeouts::SHORT_MS},
      E5Cmd{SET_DEV_EUI_CMD, std::string_view{eui.data(), eui.size()},
            SET_DEV_EUI_RSP, Timeouts::SHORT_MS},
      E5Cmd{SET_APP_KEY_CMD, params.app_key, SET_APP_KEY_RSP,
            Timeouts::SHORT_MS},
      E5Cmd{SET_EU868_CMD, {}, SET_EU868_RSP, Timeouts::SHORT_MS},
      E5Cmd{E5_DISABLE_AUTO_DATA_RATE_CMD,
            {},
            E5_DISABLE_AUTO_DATA_RATE_RSP,
            Timeouts::SHORT_MS},
      E5Cmd{SET_DATA_RATE_CMD, {}, SET_DATA_RATE_RSP, Timeouts::SHORT_MS},
#ifdef CONFIG_ENABLE_FAULT_INJECTION
      E5Cmd{
          E5_DUTY_CYCLE_OFF_CMD, {}, E5_DUTY_CYCLE_OFF_RSP, Timeouts::SHORT_MS},
      E5Cmd{E5_JOIN_DUTY_CYCLE_OFF_CMD,
            {},
            E5_JOIN_DUTY_CYCLE_OFF_RSP,
            Timeouts::SHORT_MS},
#endif
  };

  if (!std::ranges::all_of(setup_commands, [&modem, fmt](const E5Cmd &cmd) {
        if (cmd.m_arg.empty()) {
          return modem.ok(cmd.m_cmd, cmd.m_rsp, cmd.m_timeout);
        }
        return modem.ok(fmt(cmd.m_cmd, cmd.m_arg), cmd.m_rsp, cmd.m_timeout);
      })) {
    return false;
  }

  if (!modem.ok(JOIN_CMD, JOIN_RSP, Timeouts::LONG_MS) &&
      !modem.ok(FORCE_JOIN_CMD, FORCE_JOIN_RSP, Timeouts::LONG_MS)) {
    return false;
  }
  return true;
}

/**
 * @brief Build an AT+MSGHEX command for a payload into `out`. Returns the
 * command view, or nullopt if it doesn't fit.
 * @param [in] payload The payload to parse.
 * @param [out] out The output buffer.
 * @return std::optional<std::string_view>
 */
[[nodiscard]] inline std::optional<std::string_view>
build_uplink(const Payload &payload, std::span<char> out) noexcept {
  constexpr std::string_view start = MSG_START_CMD;
  constexpr std::string_view end = MSG_END_CMD;

  const auto bytes = wire_bytes(payload);
  if (out.size() < start.size() + (bytes.size() * 2) + end.size()) {
    return std::nullopt;
  }

  std::size_t out_size = 0;
  for (char c : start) {
    out[out_size++] = c;
  }

  const auto hex = to_hex(bytes, out.subspan(out_size));
  if (!hex) {
    return std::nullopt;
  }

  out_size += hex->size();
  for (char c : end) {
    out[out_size++] = c;
  }

  return std::string_view{out.data(), out_size};
}

} // namespace fog::lora
