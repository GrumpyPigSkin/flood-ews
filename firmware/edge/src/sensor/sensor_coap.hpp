#pragma once

#include "common/mutex.hpp"
#include "fmt/core.h"
#include <common/coap_utils.hpp>
#include <common/sensor_reading.hpp>
#include <cstddef>
#include <cstdint>
#include <glaze/beve.hpp>
#include <mutex>
#include <type_traits>

namespace edge::sensor {

/**
 * @brief Service to send the sensor data over CoAP to the fog layer.
 */
class CoapService {
public:
  /**
   * @brief Construct a new Coap Service object
   * @param [in] address The address to send to, should be multicast.
   * @param [in] uri_path The uri path to send on.
   */
  CoapService(const char *address, const char *uri_path)
      : m_eui(coap_utils::get_eui64_as_uint64()), m_address(address),
        m_uri_path{uri_path} {}

  /**
   * @brief Send the sensor reading over Thread through a CoAP put message.
   * @param [in] reading The sensor reading.
   * @return coap_utils::CoapErr
   */
  coap_utils::CoapErr send_sensor_data(const common::SensorReading &reading) {
    // Write to an internal buffer.
    constexpr std::size_t BUF_SIZE = 64;
    std::array<std::byte, BUF_SIZE> buffer;

    common::SensorReadingWire to_wire{.eui = m_eui,
                                      .lvl = reading.water_level,
                                      .seq = m_seq_id,
                                      .val = reading.validity,
                                      .det = reading.detail};

    auto err = glz::write_beve(to_wire, buffer);

    if (err) {
      return coap_utils::CoapErr::NO_MSG;
    }

    const std::span<const std::byte> payload_span{buffer.data(), err.count};

    // Increment the sequence id.
    m_seq_id++;

    std::lock_guard guard{m_otmx};
    return coap_utils::put_req_send_bytes(m_address, m_uri_path, payload_span,
                                          nullptr, nullptr);
  }

private:
  /** @brief The EUI64 for this node. */
  std::uint64_t m_eui{};

  /** @brief The sequence for this message. */
  std::uint8_t m_seq_id{};

  /** @brief The address to send to. */
  const char *m_address{nullptr};

  /** @brief The uri to send to. */
  const char *m_uri_path{nullptr};

  /** @brief Lock for openthread API access. */
  mutable common::openthread_mutex m_otmx;
};

} // namespace edge::sensor
