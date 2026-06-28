#pragma once

#include "common/mutex.hpp"
#include <array>
#include <openthread/ip6.h>
#include <optional>
#include <ranges>
#include <stdint.h>

namespace fog::raft {

/**
 * @brief Type to store the discovered peers.
 */
struct ServicePeer {
  std::uint64_t eui;
  otIp6Address addr;
  std::int64_t last_seen_ms;
  bool valid;
};

/**
 * @brief NetworkService encapsulates the finding and retrieving of fellow raft
 * nodes. Each raft node advertises the fact it is a raft node through a
 * service. These nodes are then added to a table of EUI's for addressing from
 * the raft algorithm.
 */
class NetworkService {
public:
  static constexpr std::size_t MAX_PEERS = 3;
  static constexpr std::uint64_t PEER_TIMEOUT_MS = 2000;
  static constexpr std::size_t RAFT_SERVICE_ENTERPRISE_NUMBER = 12345;
  static constexpr std::size_t RAFT_SERVICE_ID = 0x5A;

  /**
   * @brief Initialise the service.
   */
  void init();

  /**
   * @brief Discover peers is used to find fellow raft nodes, this should be
   * called to ensure the node list stays up to date.
   */
  void discover_peers();

  /**
   * @brief Get the EUI for the given IP.
   * @param [in] ip The ip to search for.
   */
  std::optional<std::uint64_t>
  get_eui_from_ip(const otIp6Address &ip_addr) const;

  /**
   * @brief Get the address for the given EUI.
   * @param [in] eui The eui to look for.
   * @return std::optional<otIp6Address> std::nullopt if no address was found.
   */
  std::optional<otIp6Address> get_addr_from_eui(std::uint64_t eui) const;

  /**
   * @brief Get the active peers.
   * @return auto Filter view filtered on peer.valid flag.
   */
  auto get_active_peers() const {
    return m_peers | std::views::filter(
                         [](const ServicePeer &peer) { return peer.valid; });
  }

private:
  /**
   * @brief Register the service with openthread so we advertise the capability.
   */
  void register_local_service();

  /**
   * @brief Internal helper to find or create a new peer when we Discover a
   * service. Does not allocate.
   * @param [in] eui The new eui.
   * @return ServicePeer* A pointer to the newly created instance inside
   * m_peers.
   */
  ServicePeer *find_or_create_peer(uint64_t eui);

  /**
   * @brief Get rid of peers we haven't seen for PEER_TIMEOUT_MS.
   */
  void prune_stale_peers();

  /**
   * @brief Array of peers.
   */
  std::array<ServicePeer, MAX_PEERS> m_peers{};

  /**
   * @brief This nodes eui64
   */
  std::uint64_t m_eui{};

  /**
   * @brief Mutex to protect open thread.
   */
  mutable common::openthread_mutex m_otmx;

  /**
   * @brief Mutex to protect internal state.
   */
  mutable common::mutex m_mx;
};

} // namespace fog::raft

// Include implementation.
#include "network_service_impl.hpp"
