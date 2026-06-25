#pragma once

#include "expected.hpp"
#include "scope_guard.hpp"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <openthread/coap.h>
#include <openthread/link.h>
#include <span>
#include <zephyr/net/openthread.h>

namespace coap_utils {

/**
 * @brief Address to multicast to all routers in a mesh.
 * https://openthread.io/guides/thread-primer/ipv6-addressing#multicast
 */
static constexpr auto *MESH_LOCAL_MULTICAST_ADDR = "ff03::2";

enum class CoapErr : std::uint8_t {
  OK,
  NO_INST,
  BAD_ADDR,
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

  err = otIp6AddressFromString(addr, &msg_info.mPeerAddr);

  if (err != OT_ERROR_NONE) {
    return CoapErr::BAD_ADDR;
  }

  msg_info.mPeerPort = OT_DEFAULT_COAP_PORT;

  otMessage *const msg = otCoapNewMessage(ot_inst, NULL);
  if (msg == nullptr) {
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

template <typename ByteLike = std::byte>
[[nodiscard]] CoapErr
req_send_bytes(char const *const addr, char const *const uri,
               const std::span<const ByteLike> buf,
               otCoapResponseHandler handler, void *ctx, otCoapCode code) {

  static_assert(
      sizeof(ByteLike) == 1,
      "coap_get_bytes works on byte-sized elements (std::byte/char/uint8_t)");

  otMessageInfo msg_info{};
  otError err{};

  otInstance *const ot_inst = openthread_get_default_instance();
  if (!ot_inst) {
    return CoapErr::NO_INST;
  }

  err = otIp6AddressFromString(addr, &msg_info.mPeerAddr);

  if (err != OT_ERROR_NONE) {
    return CoapErr::BAD_ADDR;
  }

  msg_info.mPeerPort = OT_DEFAULT_COAP_PORT;

  otMessage *const msg = otCoapNewMessage(ot_inst, NULL);
  if (msg == nullptr) {
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

  err = otMessageAppend(msg, buf.data(), buf.size());
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
template <typename ByteLike = std::byte>
[[nodiscard]] CoapErr
put_req_send_bytes(char const *const addr, char const *const uri,
                   const std::span<const ByteLike> buf,
                   otCoapResponseHandler handler, void *ctx) {
  return detail::req_send(addr, uri, buf, handler, ctx, OT_COAP_CODE_PUT);
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
  return detail::req_send_bytes(addr, uri, msg, handler, ctx, OT_COAP_CODE_PUT);
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

  if (static_cast<std::size_t>(coap_len) > sizeof(T)) {
    return tl::unexpected(CoapErr::FAILED_GET_MESSAGE);
  }

  T out{};
  otMessageRead(msg, otMessageGetOffset(msg), static_cast<void *>(&out),
                coap_len);
  return out;
}

/**
 * @brief Read a variable length CoAP payload into a caller buffer.
 * @tparam ByteLike defaulted to std::byte
 * @param msg The message to parse.
 * @param out The output buffer.
 * @return tl::expected<std::span<ByteLike>, CoapErr> A span over the actual
 * data, or an error.
 */
template <typename ByteLike = std::byte>
[[nodiscard]] tl::expected<std::span<ByteLike>, CoapErr>
coap_get_bytes(otMessage *const msg, std::span<ByteLike> out) {
  static_assert(
      sizeof(ByteLike) == 1,
      "coap_get_bytes works on byte-sized elements (std::byte/char/uint8_t)");

  const int length = otMessageGetLength(msg);
  const int offset = otMessageGetOffset(msg);

  // Guard the unsigned-subtraction underflow.
  if (length < 0 || offset < 0 || length < offset) {
    return tl::unexpected(CoapErr::FAILED_GET_MESSAGE);
  }

  const auto avail = static_cast<std::size_t>(length - offset);
  if (avail > out.size()) {
    // Payload doesn't fit the caller's buffer.
    return tl::unexpected(CoapErr::FAILED_GET_MESSAGE);
  }

  const uint16_t read = otMessageRead(msg, static_cast<uint16_t>(offset),
                                      out.data(), static_cast<uint16_t>(avail));
  return out.subspan(0, read);
}

inline int coap_resp_send(otMessage *const req,
                          const otMessageInfo *const req_info,
                          uint8_t const *const buf, const int len) {

  otCoapCode resp_code;
  otCoapType resp_type;
  otError err;
  int ret;

  otInstance *const ot = openthread_get_default_instance();

  if (!ot) {
    return -ENODEV;
  }

  otMessage *const resp = otCoapNewMessage(ot, NULL);

  if (!resp) {
    return -ENOMEM;
  }

  auto sg_free_msg = folly::makeGuard([&resp] { otMessageFree(resp); });

  switch (otCoapMessageGetType(req)) {
  case OT_COAP_TYPE_CONFIRMABLE:
    resp_type = OT_COAP_TYPE_ACKNOWLEDGMENT;
    break;
  case OT_COAP_TYPE_NON_CONFIRMABLE:
    resp_type = OT_COAP_TYPE_NON_CONFIRMABLE;
    break;
  default:
    return -EINVAL;
  }

  switch (otCoapMessageGetCode(req)) {
  case OT_COAP_CODE_GET:
    resp_code = OT_COAP_CODE_CONTENT;
    break;
  case OT_COAP_CODE_PUT:
    resp_code = OT_COAP_CODE_CHANGED;
    break;
  default:
    return -EINVAL;
  }

  err = otCoapMessageInitResponse(resp, req, resp_type, resp_code);
  if (err != OT_ERROR_NONE) {
    return -EBADMSG;
  }

  err = otCoapMessageSetPayloadMarker(resp);
  if (err != OT_ERROR_NONE) {
    return -EBADMSG;
  }

  err = otMessageAppend(resp, buf, len);
  if (err != OT_ERROR_NONE) {
    return -EBADMSG;
  }

  err = otCoapSendResponse(ot, resp, req_info);
  if (err != OT_ERROR_NONE) {
    return -EIO;
  }

  sg_free_msg.dismiss();

  return 0;
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

inline int coap_init() {
  otInstance *ot = openthread_get_default_instance();
  if (!ot) {
    return -ENODEV;
  }

  const otError err = otCoapStart(ot, OT_DEFAULT_COAP_PORT);
  if (err != OT_ERROR_NONE) {
    return -EBADMSG;
  }

  return 0;
}

} // namespace coap_utils
