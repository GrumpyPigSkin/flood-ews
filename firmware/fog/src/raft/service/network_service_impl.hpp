#pragma once

#include "common/logging.hpp"
#include "common/ot_utils.hpp"
#include "network_service.hpp"
#include "openthread.h"
#include <cstring>
#include <mutex>
#include <openthread/error.h>
#include <openthread/instance.h>
#include <openthread/ip6.h>
#include <openthread/netdata.h>
#include <openthread/server.h>
#include <openthread/thread.h>
#include <optional>

namespace fog::raft {

inline void NetworkService::init() {
  const std::scoped_lock guard(m_otmx);
  m_eui = common::get_eui64_as_uint64();
  register_local_service();
}

inline void NetworkService::register_local_service() {

  auto *const ot_inst = openthread_get_default_instance();
  otIp6Address my_addr;
  otIp6Address const *const ml_eid = otThreadGetMeshLocalEid(ot_inst);
  std::memcpy(&my_addr, ml_eid, sizeof(my_addr));

  // Add our custom service with the service ID and the enterprise number.
  otServiceConfig service = {0};
  service.mServiceId = RAFT_SERVICE_ID;
  service.mEnterpriseNumber = RAFT_SERVICE_ENTERPRISE_NUMBER;
  service.mServiceDataLength = sizeof(std::uint64_t);

  std::memcpy(service.mServiceData, &m_eui, sizeof(m_eui));

  service.mServerConfig.mStable = true;
  service.mServerConfig.mServerDataLength = sizeof(otIp6Address);
  std::memcpy(service.mServerConfig.mServerData, &my_addr, sizeof(my_addr));

  otError err = otServerAddService(ot_inst, &service);
  if (err != OT_ERROR_NONE) {
    logging::err("Failed to add service: {}", otThreadErrorToString(err));
    return;
  }

  // Register the service so other nodes can fid it.
  err = otServerRegister(ot_inst);
  logging::inf("Service register: {}", otThreadErrorToString(err));
}

inline void NetworkService::discover_peers() {

  const std::scoped_lock guard(m_otmx, m_mx);
  auto *const ot_inst = openthread_get_default_instance();

  otNetworkDataIterator iter = OT_NETWORK_DATA_ITERATOR_INIT;
  otServiceConfig service;

  while (otNetDataGetNextService(ot_inst, &iter, &service) == OT_ERROR_NONE) {
    if (service.mEnterpriseNumber != RAFT_SERVICE_ENTERPRISE_NUMBER ||
        service.mServiceDataLength != sizeof(std::uint64_t)) {
      continue;
    }

    std::uint64_t peer_eui;
    std::memcpy(&peer_eui, service.mServiceData, sizeof(peer_eui));
    if (peer_eui == m_eui) {
      continue;
    }

    ServicePeer *const peer = find_or_create_peer(peer_eui);

    if (peer == nullptr) {
      continue;
    }

    std::memcpy(&peer->addr, service.mServerConfig.mServerData,
                sizeof(peer->addr));
    peer->last_seen_ms = k_uptime_get();
  }

  prune_stale_peers();
}

std::optional<std::uint64_t> inline NetworkService::get_eui_from_ip(
    const otIp6Address &ip_addr) const {

  const std::scoped_lock guard(m_mx);

  for (std::size_t i = 0; i < MAX_PEERS; i++) {
    if (m_peers[i].valid &&
        (std::memcmp(&m_peers[i].addr, &ip_addr, sizeof(otIp6Address)) == 0)) {
      return m_peers[i].eui;
    }
  }

  return std::nullopt;
}

std::optional<otIp6Address> inline NetworkService::get_addr_from_eui(
    const std::uint64_t eui) const {

  const std::scoped_lock guard(m_mx);

  for (std::size_t i = 0; i < MAX_PEERS; i++) {
    if (m_peers[i].valid && m_peers[i].eui == eui) {
      return m_peers[i].addr;
    }
  }

  return std::nullopt;
}

inline ServicePeer *
NetworkService::find_or_create_peer(const std::uint64_t eui) {

  // Try and find a peer first.
  for (auto &peer : m_peers) {
    if (peer.valid && peer.eui == eui) {
      return &peer;
    }
  }

  // None found, try and create a peer.
  for (auto &peer : m_peers) {
    if (!peer.valid) {
      peer.eui = eui;
      peer.valid = true;
      logging::inf("Added new peer: {}", eui);
      return &peer;
    }
  }

  // Could not create.
  logging::wrn("Peer: {}, could not be added", eui);
  return nullptr;
}

inline void NetworkService::prune_stale_peers() {

  const auto now = k_uptime_get();

  for (std::size_t i = 0; i < MAX_PEERS; i++) {
    if (m_peers[i].valid && (now - m_peers[i].last_seen_ms) >
                                static_cast<std::int64_t>(PEER_TIMEOUT_MS)) {
      m_peers[i].valid = false;
      logging::inf("Raft peer {} timed out", m_peers[i].eui);
    }
  }
}

} // namespace fog::raft
