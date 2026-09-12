#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace fog::lora {

inline constexpr std::size_t MAX_ENTRIES = 3;

/**
 * @brief For LoRaWAN we cannot afford for additional bytes because everything
 * eats into the duty cycle we are on for. Pack the data send over the wire back
 * to the gateway.
 */
#pragma pack(push, 1)
struct Entry {
  std::uint64_t m_device_rloc{};
  std::uint16_t m_water_level_mm{};
  std::uint8_t m_validity{};
  std::uint8_t m_detail{};
  std::uint32_t m_timestamp{};
};

struct Payload {
  std::uint64_t m_node_id{};        // sending fog node id
  std::uint32_t m_raft_term{};      // term of the Raft entry being shipped
  std::uint32_t m_raft_log_index{}; // index of the Raft entry being shipped
  std::uint32_t m_sequence{};       // The sequence number for this message
  std::uint8_t m_alert{};           // 1 if alert mode active
  std::uint8_t m_count{};           // number of valid entries
  std::array<Entry, MAX_ENTRIES> entries{};
};
#pragma pack(pop)

constexpr std::size_t EXPECTED_ENTRY_SIZE = 16;
constexpr std::size_t EXPECTED_PAYLOAD_SIZE = 70;

static_assert(sizeof(Entry) == EXPECTED_ENTRY_SIZE,
              "Entry must stay wire-packed");
static_assert(sizeof(Payload) == EXPECTED_PAYLOAD_SIZE,
              "Payload must stay wire-packed");

/**
 * @brief Get the length of data to send over the wire in bytes.
 * @param [in] payload The payload.
 * @return constexpr std::size_t
 */
[[nodiscard]] constexpr std::size_t wire_len(const Payload &payload) noexcept {
  return offsetof(Payload, entries) + (payload.m_count * sizeof(Entry));
}

/**
 * @brief Get a view over a payload as raw bytes for encoding.
 * @param [in] payload
 * @return std::span<const std::byte>
 */
[[nodiscard]] inline std::span<const std::byte>
wire_bytes(const Payload &payload) noexcept {
  return {reinterpret_cast<const std::byte *>(&payload), wire_len(payload)};
}

/**
 * @brief Encode bytes as uppercase ASCII hex into `out`. Returns the written
 * view, or nullopt if `out` is too small.
 * @param [in] in The data to process into hex bytes.
 * @param [out] out The output buffer.
 * @return constexpr std::optional<std::string_view> view of bytes to be
 * written.
 */
template <typename ByteLike = std::byte>
[[nodiscard]] constexpr std::optional<std::string_view>
to_hex(std::span<const ByteLike> in, std::span<char> out) noexcept {

  static constexpr char HEX[] = "0123456789ABCDEF";

  if (out.size() < in.size() * 2) {
    return std::nullopt;
  }

  std::size_t out_size = 0;
  for (ByteLike byte : in) {
    const auto val = static_cast<std::uint8_t>(byte);
    out[out_size++] = HEX[val >> 4];
    out[out_size++] = HEX[val & 0x0F];
  }
  return std::string_view{out.data(), out_size};
}

/**
 * @brief Backoff policy for joining LoRaWAN, after each unsuccessful
 * attempt, double the duration on each attempt, capped at MAX_MS.
 */
struct Backoff {
  static constexpr std::uint64_t MIN_MS = 500;
  static constexpr std::uint64_t MAX_MS = 30000;

  /**
   * @brief Backoff policy for joining LoRaWAN, after each unsuccessful
   * attempt, double the duration on each attempt, capped at MAX_MS.
   * @param [in] attempt
   * @return constexpr std::uint32_t
   */
  [[nodiscard]] static constexpr std::uint32_t
  delay_for(std::uint32_t attempt) noexcept {
    std::uint64_t duration = MIN_MS;
    for (std::uint32_t i = 0; i < attempt && duration < MAX_MS; ++i) {
      duration <<= 1;
    }
    return static_cast<std::uint32_t>(std::min(duration, MAX_MS));
  }
};

} // namespace fog::lora
