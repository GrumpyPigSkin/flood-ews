#pragma once

#include "common/coap_utils.hpp"
#include "common/sensor_reading.hpp"
#include <cstddef>
#include <functional>
#include <glaze/beve.hpp>
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
   * @param [in] uri The URI for the CoAP resource to attach to.
   * @param [in] func The call back, this will be called in open thread context
   * so needs to be small.
   */
  Service(const char *uri, OnReadingT func)
      : m_uri(uri), m_on_reading(std::move(func)) {}

  /**
   * @brief Initialise the class, registers the CoAP resource.
   */
  void init() {

    m_resource = otCoapResource{.mUriPath = m_uri,
                                .mHandler = sensor_request_handler,
                                .mContext = this,
                                .mNext = nullptr};

    otInstance *const ot_inst = openthread_get_default_instance();
    otCoapAddResource(ot_inst, &m_resource);
  }

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
  /**
   * @brief Handle receiving a CoAP message from the sensors.
   */
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

    auto err = glz::read_beve(reading, json_payload);

    if (err) {
      return;
    }

    // Submit the reading.
    self.on_reading(reading);
  }

  /**
   * @brief The URI to attach the CoAP resource to in init.
   */
  const char *m_uri;

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
