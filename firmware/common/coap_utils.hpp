#pragma once

#include "expected.hpp"
#include "scope_guard.hpp"
#include <cstdint>
#include <cstring>
#include <openthread/coap.h>
#include <openthread/link.h>
#include <zephyr/net/openthread.h>


namespace coap_handler {

enum class CoapErr : std::uint8_t {
  OK,
  NO_INST,
  NO_MSG,
  FAILED_TO_APPEND_URI,
  FAILED_SET_PAYLOAD,
  FAILED_APPEND_PAYLOAD,
  FAILED_APPEND_MSG,
  FAILED_SEND_REQUEST,
  FAILED_GET_MESSAGE,
  COAP_INIT_FAILED,
};

namespace detail {

/**
 * @brief Generic helper to send a CoAP message.
 * @tparam T The type to send.
 * @param [in] addr The address to send to.
 * @param [in] uri The URI of the CoAP message.
 * @param [in] buf The data to send.
 * @param [in] handler The response handler, nullptr if none.
 * @param [in] ctx The response context nullptr if none.
 * @param [in] code The CoAP code, GET/PUT/POST/DELETE
 * @return CoapErr
 */
template <typename T>
CoapErr req_send(char const *const addr, char const *const uri, const T &buf,
                 otCoapResponseHandler handler, void *ctx, otCoapCode code) {
  otMessageInfo msg_info{};
  otError err{};

  otInstance *const ot_inst = openthread_get_default_instance();
  if (!ot_inst) {
    return CoapErr::NO_INST;
  }

  (void)otIp6AddressFromString(addr, &msg_info.mPeerAddr);
  msg_info.mPeerPort = OT_DEFAULT_COAP_PORT;

  otMessage *const msg = otCoapNewMessage(ot_inst, NULL);
  if (!msg) {
    return CoapErr::NO_MSG;
  }

  otCoapMessageInit(msg, OT_COAP_TYPE_CONFIRMABLE, code);

  err = otCoapMessageAppendUriPathOptions(msg, uri);

  auto sg_free_msg = folly::makeGuard([&msg] { otMessageFree(msg); });

  if (err != OT_ERROR_NONE) {

    return CoapErr::FAILED_TO_APPEND_URI;
  }

  err = otCoapMessageSetPayloadMarker(msg);
  if (err != OT_ERROR_NONE) {
    return CoapErr::FAILED_SET_PAYLOAD;
  }

  err = otMessageAppend(msg, &buf, sizeof(T));
  if (err != OT_ERROR_NONE) {
    return CoapErr::FAILED_APPEND_MSG;
  }

  err = otCoapSendRequest(ot_inst, msg, &msg_info, handler, ctx);
  if (err != OT_ERROR_NONE) {
    return CoapErr::FAILED_SEND_REQUEST;
  }

  // Disarm the guard, all was okay.
  sg_free_msg.dismiss();
  return CoapErr::OK;
}
} // namespace detail

/**
 * @brief Initialise Coap.
 */
inline CoapErr init() {

  otInstance *const ot_inst = openthread_get_default_instance();
  if (ot_inst == nullptr) {
    return CoapErr::NO_INST;
  }

  if (const auto err = otCoapStart(ot_inst, OT_DEFAULT_COAP_PORT);
      err != OT_ERROR_NONE) {
    return CoapErr::COAP_INIT_FAILED;
  }

  return CoapErr::OK;
}

/**
 * @brief Helper to send a CoAP PUT request.
 * @tparam T The type to send.
 * @param [in] addr The address to send to.
 * @param [in] uri The URI of the CoAP message.
 * @param [in] buf The data to send.
 * @param [in] handler The response handler, nullptr if none.
 * @param [in] ctx The response context nullptr if none.
 * @return CoapErr
 */
template <typename T>
CoapErr put_req_send(char const *const addr, char const *const uri,
                     const T &msg, otCoapResponseHandler handler, void *ctx) {
  return detail::req_send(addr, uri, msg, handler, ctx, OT_COAP_CODE_PUT);
}

/**
 * @brief Helper to send a CoAP GET request.
 * @tparam T The type to send.
 * @param [in] addr The address to send to.
 * @param [in] uri The URI of the CoAP message.
 * @param [in] buf The data to send.
 * @param [in] handler The response handler, nullptr if none.
 * @param [in] ctx The response context nullptr if none.
 * @return CoapErr
 */
template <typename T>
CoapErr get_req_send(char const *const addr, char const *const uri,
                     const T &msg, otCoapResponseHandler handler, void *ctx) {
  return detail::req_send(addr, uri, msg, handler, ctx, OT_COAP_CODE_GET);
}

/**
 * @brief Parse message msg into a T and return it.
 * @tparam T
 * @param msg The message to get the data from.
 * @return tl::expected<T, CoapErr> Return CoAP error on failure, T on success.
 */
template <typename T>
tl::expected<T, CoapErr> coap_get_data(otMessage *const msg) {

  const auto coap_len = otMessageGetLength(msg) - otMessageGetOffset(msg);

  if (coap_len > sizeof(T)) {
    return tl::unexpected(CoapErr::FAILED_GET_MESSAGE);
  }

  T out{};
  otMessageRead(msg, otMessageGetOffset(msg), out, coap_len);
  return out;
}

constexpr std::size_t EUI64_LEN = 8;
using Eui64Arr = std::array<std::uint8_t, EUI64_LEN>;

/**
 * @brief Get the eui64
 * @return otExtAddress
 */
inline otExtAddress get_eui64() {
  otInstance *const ot_inst = openthread_get_default_instance();
  otExtAddress eui64;
  otLinkGetFactoryAssignedIeeeEui64(ot_inst, &eui64);
  return eui64;
}

/**
 * @brief Get the eui64 as a uint64_t
 * @return std::uint64_t
 */
inline std::uint64_t get_eui64_as_uint64() {
  const auto eui = get_eui64();
  return sys_get_be64(eui.m8);
}

/**
 * @brief Get the eui64 as an std::array<std::uint8_t, EUI64_LEN>;
 * @return std::array<std::uint8_t, EUI64_LEN>;
 */
inline Eui64Arr get_eui64_as_arr8() {
  const auto eui = get_eui64();
  Eui64Arr eui_out;
  std::memcpy(eui_out.data(), eui.m8, eui_out.size());
  return eui_out;
}

} // namespace coap_handler
