#pragma once

#include "common/coap_utils.h"
#include "common/logging.hpp"
#include "common/security/trusted_device_store.hpp"
#include "common/sensor_reading.hpp"
#include "security/sensor_replay_detection.hpp"
#include <functional>
#include <openthread/coap.h>
#include <openthread/network_time.h>

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
  Service(const char *uri, OnReadingT func, common::TrustedDeviceStore &tds)
      : m_uri(uri), m_on_reading(std::move(func)),
        m_trusted_device_store(&tds) {}

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
  void on_reading(common::SensorReadingSigned msg) {
    otInstance *const ot_inst = openthread_get_default_instance();
    uint64_t network_time_us = 0;
    otNetworkTimeStatus status = otNetworkTimeGet(ot_inst, &network_time_us);

    if (status != OT_NETWORK_TIME_SYNCHRONIZED) {
      logging::wrn("on_reading: Network time not synced.");
    }

    auto &reading = msg.m_reading;

    const auto res = m_trusted_device_store->verify(
        reading.m_eui,
        std::span(reinterpret_cast<std::uint8_t *>(&reading), sizeof(reading)),
        msg.m_signature);

    if (res != PSA_SUCCESS) {
      logging::inf("Bad sensor payload from .eui={}, .error={}", reading.m_eui,
                   res);
      return;
    }

    logging::inf("on_reading: got new reading: .eui={}, .reading={}",
                 reading.m_eui, reading.m_water_level_mm);

    // Add the timestamp.
    reading.m_timestamp = network_time_us;

    // Check this isn't a duplicate.
    if (!m_replay_detect.is_fresh(reading)) {
      return;
    }

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
    common::SensorReadingSigned signed_reading{};

    int len = sizeof(signed_reading);
    const auto res = coap_get_data(msg, &signed_reading, &len);

    if (res != 0 || len != sizeof(signed_reading)) {
      return;
    }

    // Submit the reading.
    self.on_reading(signed_reading);
  }

  /** @brief The URI to attach the CoAP resource to in init. */
  const char *m_uri;

  /** @brief The resource for registering coap handle. */
  otCoapResource m_resource;

  /** @brief Callback when we get a reading. */
  OnReadingT m_on_reading;

  /** @brief Trusted devices for signature verification. */
  common::TrustedDeviceStore *m_trusted_device_store;

  /** @brief Replay detection. */
  security::SensorReplayDetection m_replay_detect;
};

} // namespace fog::sensor
