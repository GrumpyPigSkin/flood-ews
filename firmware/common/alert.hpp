#pragma once

#include "coap_utils.h"
#include "mutex.hpp"
#include "ot_utils.hpp"
#include <functional>
#include <mutex>
#include <openthread/coap.h>
#include <openthread/network_time.h>

namespace common {

/**
 * @brief Handle sending and receiving an alert.
 */
class Alert {

  static constexpr auto *ALERT_URI = "alert";
  static constexpr auto *ALERT_ADDR = common::MESH_LOCAL_FTD_MTD_MULTICAST_ADDR;

  /**
   * @brief The packet sent over the thread mesh.
   */
  struct AlertPacket {
    /**
     * @brief When we get an alert, the best action I think we can take is to
     * just reschedule timers.
     */
    std::uint16_t m_alert_interval;
    bool m_alert_active;
  };

  /** @brief Callback type. */
  using OnAlertT = std::function<void(const AlertPacket)>;

public:
  /**
   * @brief Constructor
   * @param [in] on_alert callback called in openthread conetext!
   */
  Alert(OnAlertT on_alert) : m_on_alert(std::move(on_alert)) {}

  /**
   * @brief Initialise the class, registers the CoAP resource.
   */
  void init() {

    m_resource = otCoapResource{.mUriPath = ALERT_URI,
                                .mHandler = alert_handler,
                                .mContext = this,
                                .mNext = nullptr};

    otInstance *const ot_inst = openthread_get_default_instance();
    otCoapAddResource(ot_inst, &m_resource);
  }

  /**
   * @brief Handle sending an alert.
   * @param [in] packet the alert to send.
   * @return int
   */
  int send_alert(const AlertPacket packet) const noexcept {
    std::scoped_lock guard{m_otmx};

    coap_addr_t addr;
    addr.u.str = ALERT_ADDR;
    addr.is_str = true;

    return coap_put_req_send(addr, ALERT_URI,
                             reinterpret_cast<const uint8_t *>(&packet),
                             sizeof(packet), nullptr, nullptr);
  }

private:
  /**
   * @brief Handle receiving a CoAP message from the sensors.
   */
  static void alert_handler(void *ctx, otMessage *msg,
                            otMessageInfo const * /*i*/) noexcept {
    auto &self = *static_cast<Alert *>(ctx);
    AlertPacket alert{};

    int len = sizeof(alert);
    const auto res = coap_get_data(msg, &alert, &len);

    if (res != 0 || len != sizeof(alert)) {
      return;
    }

    // Submit the alert.
    self.m_on_alert(alert);
  }

  /** @brief The resource for registering coap handle. */
  otCoapResource m_resource;

  /** @brief Handle an alert message. */
  OnAlertT m_on_alert;

  /** @brief Lock for openthread API access. */
  mutable common::openthread_mutex m_otmx;
};

} // namespace common
