#pragma once

#include "common/coap_utils.h"
#include "common/inplace_function.hpp"
#include "common/logging.hpp"
#include "common/ot_utils.hpp"
#include "common/security/trusted_device_store.hpp"
#include "raft/raft_types.hpp"
#include "raft/service/network_service.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <openthread/coap.h>

namespace fog::raft {

class CoapServer {
  using InboundFunc = stdext::inplace_function<bool(const Message<> &)>;
  static constexpr std::size_t NUM_COAP_ENDPOINTS = 6;

public:
  /**
   * @brief Constructor
   * @param [in] func Callback when a message is received.
   * @param [in] service Service for finding peer addresses.
   */
  CoapServer(InboundFunc func, NetworkService &service,
             common::TrustedDeviceStore &tds)
      : m_peer_service(&service), m_on_inbound(std::move(func)),
        m_trusted_devices(&tds) {}

  /**
   * @brief Initialise the class, registers CoAP resources.
   */
  void init();

  /**
   * @brief Send a message over coap to a peer.
   * @param [in] msg Outgoing message.
   */
  void send_msg(const Message<> &msg);

  /**
   * @brief Decode a response into a MessageT to send into raft.
   * @tparam T
   * @param [in] msg The actual message data.
   * @param [in] info Message info.
   * @param [in] resolve_peer Whether or not the URI need from to be resolved
   * and added to the message.
   * @return true on success.
   */
  template <typename T>
  bool decode_response(otMessage *msg, const otMessageInfo * /*info*/,
                       bool resolve_peer) {

    constexpr std::size_t ENVELOPE_LEN = sizeof(std::uint64_t) +
                                         sizeof(std::uint32_t) + sizeof(T) +
                                         common::TrustedDeviceStore::SIG_LEN;

    std::array<std::uint8_t, ENVELOPE_LEN> buf{};
    int len = static_cast<int>(ENVELOPE_LEN);
    if (coap_get_data(msg, buf.data(), &len) != 0 ||
        len != static_cast<int>(ENVELOPE_LEN)) {
      logging::err("parse/size error");
      return false;
    }

    std::size_t off = 0;
    std::uint64_t sender_eui = 0;
    std::memcpy(&sender_eui, buf.data() + off, sizeof(sender_eui));
    off += sizeof(sender_eui);

    const std::uint8_t *payload_ptr = buf.data() + off;
    off += sizeof(T);

    common::TrustedDeviceStore::SignatureT sig{};
    std::memcpy(sig.data(), buf.data() + off, sig.size());

    std::span<const std::uint8_t> payload_bytes{payload_ptr, sizeof(T)};
    auto status = m_trusted_devices->verify(sender_eui, payload_bytes, sig);
    if (status != PSA_SUCCESS) {
      logging::err("raft::CoapServer: verify failed, claimed_eui={} status={}",
                   static_cast<unsigned long long>(sender_eui), status);
      return false;
    }

    Message<> item;
    if (resolve_peer) {
      item.from = sender_eui;
    }

    T rpc{};
    std::memcpy(&rpc, payload_ptr, sizeof(T));
    item.payload = rpc;
    return m_on_inbound(item);
  }

  /**
   * @brief Generic PUT to sign and send T over Thread to other nodes.
   * @param [in] addr The address to send to.
   * @param [in] uri The URI.
   * @param [in] payload The payload.
   */
  template <typename T>
  void coap_put(const otIp6Address &addr, const char *uri, const T &payload) {

    constexpr std::size_t ENVELOPE_LEN = sizeof(std::uint64_t) +
                                         sizeof(std::uint32_t) + sizeof(T) +
                                         common::TrustedDeviceStore::SIG_LEN;

    std::span<const std::uint8_t> payload_bytes{
        reinterpret_cast<const std::uint8_t *>(&payload), sizeof(T)};

    const auto sig = m_trusted_devices->sign(payload_bytes);

    if (!sig.has_value()) {
      logging::err("raft coap: sign failed for {}, .err={}", uri, sig.error());
      return;
    }

    std::array<std::uint8_t, ENVELOPE_LEN> buf{};
    std::size_t off = 0;
    std::memcpy(buf.data() + off, &m_own_eui, sizeof(m_own_eui));
    off += sizeof(m_own_eui);
    std::memcpy(buf.data() + off, &payload, sizeof(T));
    off += sizeof(T);
    std::memcpy(buf.data() + off, sig.value().data(), sig.value().size());

    coap_put_req_send({addr, false}, uri, buf.data(),
                      static_cast<int>(buf.size()), nullptr, nullptr);
  }

private:
  /** @brief CoAP resources to listen for from other peers. */
  std::array<otCoapResource, NUM_COAP_ENDPOINTS> m_resources;

  /** @brief Service for resolving peer IP addresses. */
  NetworkService *m_peer_service;

  /** @brief Callback for when a message is received. */
  InboundFunc m_on_inbound;

  common::TrustedDeviceStore *m_trusted_devices;

  std::uint64_t m_own_eui{common::get_eui64_as_uint64()};
};

} // namespace fog::raft
