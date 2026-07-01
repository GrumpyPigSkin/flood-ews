#pragma once

#include "common/mutex.hpp"
#include <common/coap_utils.h>
#include <common/ot_utils.hpp>
#include <common/sensor_reading.hpp>
#include <cstddef>
#include <cstdint>
#include <glaze/beve.hpp>
#include <mutex>

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
      : m_eui(common::get_eui64_as_uint64()), m_address(address),
        m_uri_path{uri_path} {}

  /**
   * @brief Send the sensor reading over Thread through a CoAP put message.
   * @param [in] reading The sensor reading.
   * @return coap_utils::CoapErr
   */
  int send_sensor_data(const common::SensorReading &reading) {

    common::SensorReadingWire to_wire{.m_eui = m_eui,
                                      .m_water_level_mm = reading.m_water_level,
                                      .m_validity = reading.m_validity,
                                      .m_detail = reading.m_detail,
                                      .m_seq = m_seq_id};

    // Increment the sequence id.
    m_seq_id++;

    coap_addr_t addr;
    addr.u.str = m_address;
    addr.is_str = true;

    std::lock_guard guard{m_otmx};
    return coap_put_req_send(addr, m_uri_path,
                             reinterpret_cast<const uint8_t *>(&to_wire),
                             sizeof(to_wire), nullptr, nullptr);
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
