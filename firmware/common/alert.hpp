#pragma once

#include "coap_utils.h"
#include "common/logging.hpp"
#include "common/security/trusted_device_store.hpp"
#include "inplace_function.hpp"
#include "mutex.hpp"
#include "ot_utils.hpp"
#include <array>
#include <cstdint>
#include <functional>
#include <mutex>
#include <openthread.h>
#include <openthread/coap.h>
#include <openthread/ip6.h>
#include <openthread/network_time.h>
#include <openthread/thread.h>

namespace common {

/**
 * @brief Handle sending and receiving an alert.
 */
class Alert {
public:
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
    std::uint16_t m_alert_interval_ms;
    bool m_alert_active;
  };

  struct SignedAlertPacket {
    std::uint64_t m_eui;
    AlertPacket m_packet;
    std::array<std::uint8_t, 64> m_signature;
  };

  /** @brief Callback type. */
  using OnAlertT = stdext::inplace_function<void(const AlertPacket)>;

  /**
   * @brief Constructor
   * @param [in] on_alert callback called in openthread conetext!
   */
  Alert(OnAlertT on_alert)
      : m_on_alert(std::move(on_alert)), m_eui(common::get_eui64_as_uint64()) {}

  /**
   * @brief Initialise the class, registers the CoAP resource.
   */
  void init() {
    if (const auto err = m_trusted_devices.init(m_eui); err != PSA_SUCCESS) {
      logging::err("Failed to setup signature store");
    }

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
  int send_alert(const std::uint64_t eui,
                 const AlertPacket packet) const noexcept {
    std::scoped_lock guard{m_otmx};

    otInstance *const inst = openthread_get_default_instance();

    // Fetch the Mesh-Local Prefix.
    const otMeshLocalPrefix *ml_prefix = otThreadGetMeshLocalPrefix(inst);

    auto sig = m_trusted_devices.sign(std::span(
        reinterpret_cast<const std::uint8_t *>(&packet), sizeof(packet)));

    if (!sig.has_value()) {
      logging::err("Failed to sign reading");
      return -1;
    }

    SignedAlertPacket signed_packet{
        .m_eui = m_eui, .m_packet = packet, .m_signature = sig.value()};

    // For SSEDs we need to build the multicast address:
    // https://openthread.io/guides/thread-primer/ipv6-addressing#multicast

    // Build the RFC 3306 Prefix-Based Multicast Address structure
    otIp6Address ssed_addr;
    std::memset(&ssed_addr, 0, sizeof(ssed_addr));

    ssed_addr.mFields.m8[0] = 0xff; // Multicast
    ssed_addr.mFields.m8[1] = 0x33; // Flags = 3, Scope = 3 (Mesh-Local)
    ssed_addr.mFields.m8[2] = 0x00; // Reserved
    ssed_addr.mFields.m8[3] = 0x40; // Prefix length (64 bits = 0x40)
    std::memcpy(&ssed_addr.mFields.m8[4], ml_prefix->m8,
                8);                  // Inject network prefix
    ssed_addr.mFields.m8[15] = 0x01; // Group ID = 1 (All Nodes)

    coap_addr_t addr;
    addr.u.addr = ssed_addr;
    addr.is_str = false;

    // Send
    return coap_put_req_send(addr, ALERT_URI,
                             reinterpret_cast<const uint8_t *>(&signed_packet),
                             sizeof(signed_packet), nullptr, nullptr);
  }

private:
  /**
   * @brief Handle receiving a CoAP message from the sensors.
   */
  static void alert_handler(void *ctx, otMessage *msg,
                            otMessageInfo const *info) noexcept {

    auto &self = *static_cast<Alert *>(ctx);

    otInstance *const inst = openthread_get_default_instance();
    const otIp6Address *mine = otThreadGetMeshLocalEid(inst);
    if (mine && info && otIp6IsAddressEqual(&info->mPeerAddr, mine)) {
      return; // our own multicast echo
    }

    SignedAlertPacket alert{};

    int len = sizeof(alert);
    const auto res = coap_get_data(msg, &alert, &len);

    if (res != 0 || len != sizeof(alert)) {
      return;
    }

    const auto sign_res = self.m_trusted_devices.verify(
        alert.m_eui,
        std::span(reinterpret_cast<std::uint8_t *>(&alert.m_packet),
                  sizeof(alert.m_packet)),
        alert.m_signature);

    if (sign_res != PSA_SUCCESS) {
      logging::err("alert: Failed to sign payload.");
    }

    // Submit the alert.
    self.m_on_alert(alert.m_packet);
  }

  /** @brief The resource for registering coap handle. */
  otCoapResource m_resource;

  /** @brief Handle an alert message. */
  OnAlertT m_on_alert;

  /** @brief Used for signing and verification. */
  common::TrustedDeviceStore m_trusted_devices;

  /** @brief This devices EUI. */
  std::uint64_t m_eui;

  /** @brief Lock for openthread API access. */
  mutable common::openthread_mutex m_otmx;
};

} // namespace common
