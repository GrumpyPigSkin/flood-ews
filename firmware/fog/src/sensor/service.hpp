#pragma once

#include "common/coap_utils.h"
#include "common/sensor_reading.hpp"
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
    auto &self = *static_cast<Service *>(ctx);
    SensorReadingWire reading{};

    int len = sizeof(reading);
    const auto res = coap_get_data(msg, &reading, &len);

    if (res == 0 || len != sizeof(reading)) {
      // Bad payload.
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
