#pragma once

#include "callback.hpp"
#include "coap/coap.hpp"
#include "common/logging.hpp"
#include "common/message_queue.hpp"
#include "common/periodic_task.hpp"
#include "common/security/trusted_device_store.hpp"
#include "common/work_task.hpp"
#include "fs/persist.hpp"
#include "raft_types.hpp"
#include "server/server.hpp"
#include "service/network_service.hpp"
#include <cstddef>
#include <openthread/link.h>
#include <zephyr/random/random.h>

namespace fog::raft {

class Engine {

  /**
   * @brief The raft nodes we expect to be part of the system.
   */
  static constexpr std::array<NodeId, 3> EXPECTED_EUIS = {
      0xf4ce360a4eac3643,
      0xf4ce363a5c916092,
      0xf4ce3696247825d1,
  };

  static constexpr k_timeout_t TICK = common::ms_to_k_timeout(100);
  static constexpr k_timeout_t DISCOVER_TICK = common::ms_to_k_timeout(1000);
  static constexpr std::size_t QUEUE_DEPTH = 8;

public:
  using OnApplyCallbackT = std::function<void(const Entry<> &)>;
  using OnStateChangeCallbackT =
      std::function<void(const State old_state, const State new_state)>;

  /**
   * @brief Constructor
   * @param [in] cb Called when apply is called.
   * @param [in] self_id This nodes ID
   */
  Engine(OnApplyCallbackT oacb, OnStateChangeCallbackT sccb, NodeId self_id,
         common::TrustedDeviceStore &tds)
      : m_coap([this](const auto &msg) { return receive(msg); },
               m_network_service, tds),
        m_server(self_id, EXPECTED_EUIS, make_cbs(std::move(sccb))),
        m_tick([this] { m_server.periodic(); }),
        m_message_pending_work([this] { drain(); }),
        m_discover_peers_work([this] { m_network_service.discover_peers(); }),
        m_on_apply(std::move(oacb)) {}

  void init() {
    if (auto err = m_persistence.init(); err != 0) {
      logging::inf("Persistence init failed: {}", err);
    }
    m_coap.init();
    m_network_service.init();
  }

  /**
   * @brief Start the raft server.
   */
  void start() {
    const auto loaded = m_persistence.load();
    m_server.restore_state(loaded.m_term, loaded.m_voted_for);
    m_server.start();
    m_tick.start(TICK);
    m_discover_peers_work.start(DISCOVER_TICK);
  }

  /**
   * @brief Stop the raft server.
   */
  void stop() { m_tick.stop(); }

  /**
   * @brief Submit new work to be replicated across the raft nodes.
   * @param [in] data
   */
  auto submit(const EntryType type, const std::span<const std::byte> data) {
    return m_server.submit(type, data);
  }

  /**
   * @brief Are we the leader.
   * @return true if we are.
   */
  [[nodiscard]] bool is_leader() const noexcept { return m_server.is_leader(); }

  /** @brief an entry submitted at `index`. */
  [[nodiscard]] Server<>::EntryT const *get_entry(const Index index) {
    return m_server.get_entry(index);
  }

private:
  /**
   * @brief Called when a new message comes in over Openthread
   * @param [in] msg The new message.
   * @return true
   */
  bool receive(const Message<> &msg) {
    if (!m_rx_queue.try_put(msg)) {
      logging::wrn("Raft RX queue full, dropping msg from {}", msg.from);
      return false;
    }
    m_message_pending_work.submit();
    return true;
  }

  /**
   * @brief Make the callbacks for raft server.
   * @return Callbacks<DefaultConfig>
   */
  Callbacks<DefaultConfig> make_cbs(OnStateChangeCallbackT osccb) {
    return {.m_send =
                [this](const Server<>::MessageT &msg) { m_coap.send_msg(msg); },
            .m_apply =
                [this](const Server<>::EntryT &entry) {
                  if (m_on_apply) {
                    m_on_apply(entry);
                  }
                },
            .m_persist_state =
                [this](Term current_term, NodeId voted_for) {
                  logging::inf(
                      "Persistence saved: .current_term={}, .voted_for={}",
                      current_term, voted_for);
                  m_persistence.save({current_term, voted_for});
                },
            .m_snapshot_save = [](std::uint8_t * /*buf*/,
                                  std::uint32_t /*cap*/) { return 0; },
            .m_now = [] { return static_cast<Time>(k_uptime_get()); },
            .m_rand = [] { return sys_rand32_get(); },
            .m_on_state_change = std::move(osccb)};
  }

  /**
   * @brief Drain the queue for any messages that were sent, call handle for
   * each message.
   */
  void drain() {
    m_rx_queue.drain(
        [this](const Server<>::MessageT &msg) { m_server.handle(msg); });
  }

  /** @brief The coap resources for raft. */
  CoapServer m_coap;

  /** @brief The network discovery service. */
  NetworkService m_network_service;

  /** @brief The Raft server. */
  Server<DefaultConfig> m_server;

  /** @brief Periodic work for running the main heartbeat/timeout task. */
  common::PeriodicTask m_tick;

  /** @brief Work task for when a new message is delivered from the openthread
   * workqueue. */
  common::WorkTask m_message_pending_work;

  /** @brief Queue to store received messages. */
  common::MessageQueue<Server<>::MessageT, QUEUE_DEPTH> m_rx_queue;

  /** @brief Periodic task to discover new raft nodes. */
  common::PeriodicTask m_discover_peers_work;

  /** @brief Callback for when apply is called. */
  OnApplyCallbackT m_on_apply;

  /** @brief Persistence for current term and voted for. */
  fog::fs::RaftPersistence m_persistence;
};

} // namespace fog::raft
