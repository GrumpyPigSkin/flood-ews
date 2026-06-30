#pragma once

#include "common/logging.hpp"
#include "raft/raft_types.hpp"
#include "raft/service/network_service.hpp"
#include <array>
#include <cstddef>
#include <functional>
#include <openthread/coap.h>

namespace fog::raft {

class CoapServer {
  using InboundFunc = std::function<bool(const Message<> &)>;
  static constexpr std::size_t NUM_COAP_ENDPOINTS = 6;

public:
  /**
   * @brief Constructor
   * @param [in] func Callback when a message is received.
   * @param [in] service Service for finding peer addresses.
   */
  CoapServer(InboundFunc func, NetworkService &service)
      : m_peer_service(&service), m_on_inbound(std::move(func)) {}

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
  bool decode_response(otMessage *msg, const otMessageInfo *info,
                       bool resolve_peer) {
    Message<> item;
    if (resolve_peer) {
      auto from = m_peer_service->get_eui_from_ip(info->mPeerAddr);
      if (!from) {
        logging::err("unknown peer");
        return false;
      }
      item.from = *from;
    }
    T rpc{};
    int len = sizeof(T);
    if (coap_get_data(msg, &rpc, &len) != 0 || len != sizeof(T)) {
      logging::err("parse/size error");
      return false;
    }
    item.payload = rpc;
    return m_on_inbound(item);
  }

private:
  /** @brief CoAP resources to listen for from other peers. */
  std::array<otCoapResource, NUM_COAP_ENDPOINTS> m_resources;

  /** @brief Service for resolving peer IP addresses. */
  NetworkService *m_peer_service;

  /** @brief Callback for when a message is received. */
  InboundFunc m_on_inbound;
};

} // namespace fog::raft
