/**
 * Special CoAP message for injecting faults into the sensor.
 */

#pragma once

#include "common/coap_utils.h"
#include "common/mutex.hpp"
#include "common/sensor_reading.hpp"
#include <mutex>
#include <openthread/ip6.h>

namespace edge::fault {

/** @brief Wrapper around a malicious sensor message.*/
struct FaultMessage {
  common::SensorReadingWire m_bad_reading;
  bool m_active;
  bool m_ruin_hash;
};

/** @brief URI for the fault endpoint. */
constexpr auto *FAULT_URI = "edge_fault";

/**
 * @brief Enable testing by injecting malicious values into the sensor. When
 * active this class is meant to override the sensor readings that would
 * normally be measured from the sensor.
 */
class FaultInjection {
public:
  /**
   * @brief Initialise the coap resource.
   */
  void init() {

    m_resource = otCoapResource{.mUriPath = FAULT_URI,
                                .mHandler = fault_request_handler,
                                .mContext = this,
                                .mNext = nullptr};

    otInstance *const ot_inst = openthread_get_default_instance();
    otCoapAddResource(ot_inst, &m_resource);
  }

  /**
   * @brief Get the fault message.
   * @return FaultMessage
   */
  FaultMessage get_fault() const {
    std::scoped_lock guard(m_mutex);
    return m_message;
  }

private:
  /**
   * @brief Called from within the request handler.
   * @param [in] msg The message.
   */
  void on_message(const FaultMessage msg) {
    std::scoped_lock guard(m_mutex);
    m_message = msg;
  }

  /**
   * @brief Trampoline function to handle a fault injection request.
   * @param [in] ctx
   * @param [in] msg
   * @param [in] info
   */
  static void fault_request_handler(void *ctx, otMessage *msg,
                                    otMessageInfo const *info) {
    auto &self = *static_cast<FaultInjection *>(ctx);
    FaultMessage message{};

    int len = sizeof(message);
    const auto res = coap_get_data(msg, &message, &len);

    if (res != 0 || len != sizeof(message)) {
      return;
    }

    // Submit the reading.
    self.on_message(message);

    // Send a response to confirm to the testing harness we got the message.
    coap_resp_send(msg, info, nullptr, 0);
  }

  /**
   * @brief The resource for registering coap handle.
   */
  otCoapResource m_resource{};

  /** @brief Protect m_message. */
  mutable common::mutex m_mutex;

  /** @brief The latest message received. */
  FaultMessage m_message{};
};

} // namespace edge::fault
