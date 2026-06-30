#include "coap.hpp"
#include "common/coap_utils.h"
#include "common/mutex.hpp"
#include "common/overload.hpp"
#include "openthread.h"
#include "raft/raft_types.hpp"
#include <mutex>
#include <openthread/ip6.h>

using namespace fog::raft;

namespace {

constexpr auto *RAFT_REQUEST_VOTE_URI = "raft/v";
constexpr auto *RAFT_VOTE_REPLY_URI = "raft/vr";
constexpr auto *RAFT_APPEND_URI = "raft/a";
constexpr auto *RAFT_APPEND_RESPONSE_URI = "raft/ar";
constexpr auto *RAFT_SNAP_URI = "raft/s";
constexpr auto *RAFT_SNAP_RESP_URI = "raft/sr";

/**
 * @brief Generic PUT to send T over Thread to other nodes.
 * @param [in] addr
 * @param [in] uri
 * @param [in] payload
 */
template <typename T>
void coap_put(const otIp6Address &addr, const char *uri, const T &payload) {

  coap_put_req_send({addr, false}, uri,
                    reinterpret_cast<const uint8_t *>(&payload), sizeof(T),
                    nullptr, nullptr);
}

/**
 * @brief Handlers for each URI endpoint.
 */

void on_request_vote(void *ctx, otMessage *msg, const otMessageInfo *msg_inf) {
  ARG_UNUSED(msg_inf);
  auto &self = *static_cast<CoapServer *>(ctx);
  self.decode_response<RequestVote>(msg, msg_inf, false);
}

void on_vote_resp(void *ctx, otMessage *msg, const otMessageInfo *msg_inf) {
  auto &self = *static_cast<CoapServer *>(ctx);
  self.decode_response<RequestVoteResp>(msg, msg_inf, true);
}

void on_append(void *ctx, otMessage *msg, const otMessageInfo *msg_inf) {
  ARG_UNUSED(msg_inf);
  auto &self = *static_cast<CoapServer *>(ctx);
  self.decode_response<AppendEntries<>>(msg, msg_inf, false);
}

void on_append_resp(void *ctx, otMessage *msg, const otMessageInfo *msg_inf) {
  auto &self = *static_cast<CoapServer *>(ctx);
  self.decode_response<AppendEntriesResp>(msg, msg_inf, true);
}

void on_snap(void *ctx, otMessage *msg, const otMessageInfo *msg_inf) {
  ARG_UNUSED(msg_inf);
  auto &self = *static_cast<CoapServer *>(ctx);
  self.decode_response<InstallSnapshot<>>(msg, msg_inf, false);
}

void on_snap_resp(void *ctx, otMessage *msg, const otMessageInfo *msg_inf) {
  auto &self = *static_cast<CoapServer *>(ctx);
  self.decode_response<InstallSnapshotResp>(msg, msg_inf, true);
}

} // namespace

namespace fog::raft {

void CoapServer::init() {

  struct entry {
    const char *uri;
    otCoapRequestHandler h;
  };

  const entry entries[] = {
      {RAFT_REQUEST_VOTE_URI, &on_request_vote},
      {RAFT_VOTE_REPLY_URI, &on_vote_resp},
      {RAFT_APPEND_URI, &on_append},
      {RAFT_APPEND_RESPONSE_URI, &on_append_resp},
      {RAFT_SNAP_URI, &on_snap},
      {RAFT_SNAP_RESP_URI, &on_snap_resp},
  };

  auto *const ot_inst = openthread_get_default_instance();

  for (size_t i = 0; i < m_resources.size(); ++i) {
    m_resources[i].mUriPath = entries[i].uri;
    m_resources[i].mHandler = entries[i].h;
    m_resources[i].mContext = this;
    m_resources[i].mNext = nullptr;
    common::openthread_mutex otmx;
    std::lock_guard guard(otmx);
    otCoapAddResource(ot_inst, &m_resources[i]);
  }
}

void CoapServer::send_msg(const Message<> &msg) {

  if (auto addr_opt = m_peer_service->get_addr_from_eui(msg.to)) {
    const auto addr = addr_opt.value();

    common::openthread_mutex otmx;
    std::lock_guard guard(otmx);

    auto overloads = tl::overload(
        [&](const RequestVote &playload) {
          coap_put(addr, RAFT_REQUEST_VOTE_URI, playload);
        },
        [&](const RequestVoteResp &playload) {
          coap_put(addr, RAFT_VOTE_REPLY_URI, playload);
        },
        [&](const AppendEntries<> &playload) {
          coap_put(addr, RAFT_APPEND_URI, playload);
        },
        [&](const AppendEntriesResp &playload) {
          coap_put(addr, RAFT_APPEND_RESPONSE_URI, playload);
        },
        [&](const InstallSnapshot<> &playload) {
          coap_put(addr, RAFT_SNAP_URI, playload);
        },
        [&](const InstallSnapshotResp &playload) {
          coap_put(addr, RAFT_SNAP_RESP_URI, playload);
        });

    std::visit(overloads, msg.payload);
  }
}

} // namespace fog::raft
