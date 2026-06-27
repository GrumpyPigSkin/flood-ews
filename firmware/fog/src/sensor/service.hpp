#pragma once

#include "common/coap_utils.hpp"
#include "common/logging.hpp"
#include "common/sensor_reading.hpp"
#include <cstddef>
#include <functional>
#include <openthread/coap.h>

namespace fog::sensor {

/**
 * @brief Handle messages sent from sensors to the backhaul nodes.
 */
class Service {

  using SensorReadingWire = common::SensorReadingWire;

public:
  /**
   * @brief Callback when we get a reading.
   */
  using OnReadingT = std::function<void(const SensorReadingWire &)>;

  /**
   * @brief Constructor
   * @param [in] func The call back, this will be called in open thread context
   * so needs to be small.
   */
  Service(OnReadingT func) : m_on_reading(std::move(func)) {}

  /**
   * @brief Initialise the class, registers the CoAP resource.
   */
  void init();

  /**
   * @brief On a message call back into out callback.
   * @param [in] e
   */
  void on_reading(const SensorReadingWire &reading) {
    if (m_on_reading) {
      m_on_reading(reading);
    }
  }

private:
  static void sensor_request_handler(void *ctx, otMessage *msg,
                                     otMessageInfo const * /*i*/) {

    static constexpr std::size_t BUF_SIZE = 128;
    auto &self = *static_cast<Service *>(ctx);
    SensorReadingWire reading{};

    std::array<char, BUF_SIZE> json_payload{};

    const auto res = coap_utils::coap_get_bytes(
        msg, std::span(json_payload.begin(), json_payload.size()));

    if (!res.has_value()) {
      // Bad payload.
      return;
    }

    // Direct memory parsing out of your string_view buffer.
    auto err = glz::read_json(reading, json_payload);

    if (err) {
      return;
    }

    // Data is fully verified and populated.
    logging::inf("received sensor reading: .eui={:x}, .water_level={:x}",
                 reading.eui, reading.water_level);

    // Submit the reading.
    self.on_reading(reading);
  }

  /**
   * @brief The resource for registering coap handle.
   */
  otCoapResource m_resource;

  /**
   * @brief Callback when we get a reading.
   */
  OnReadingT m_on_reading;
};

} // namespace fog::sensor
