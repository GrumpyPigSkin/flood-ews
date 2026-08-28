#pragma once

#include "common/logging.hpp"
#include "common/message_queue.hpp"
#include "common/ot_utils.hpp"
#include "common/periodic_task.hpp"
#include "lorawan/join.hpp"
#include "lorawan/modem.hpp"
#include "lorawan/protocol.hpp"
#include <atomic>
#include <cstdint>
#include <zephyr/kernel.h>

#ifdef CONFIG_ENABLE_FAULT_INJECTION
#include "fault_injection/fault_injection.hpp"
#endif

namespace fog::lora {

inline constexpr std::size_t TX_QUEUE_DEPTH = 8;

/**
 * @brief Everything the LoraWanService needs from the platform.
 */
struct Platform {
  UartPort uart{};
  std::array<std::uint8_t, common::EUI64_LEN> eui{};
  std::string_view app_key;
  stdext::inplace_function<void(std::uint32_t seq, std::uint64_t batch_index)>
      m_on_complete;
};

class LoraWanService {

  static constexpr std::size_t BUFFER_SIZE = 256;
  static constexpr std::size_t SCRATCH_BUFFER_SIZE = 64;

public:
  /**
   * @brief Constructor
   * @param [in] platform dependencies to run the service.
   */
  explicit LoraWanService(Platform platform)
      : m_eui{platform.eui}, m_app_key{platform.app_key},
        m_modem{std::move(platform.uart)},
        m_on_complete(std::move(platform.m_on_complete)) {
    k_sem_init(&m_join_sem, 0, 1);
  }

  /** @brief Deleted copy and move constructors as we own a thread. */
  LoraWanService(const LoraWanService &) = delete;
  LoraWanService(LoraWanService &&) = delete;
  LoraWanService &operator=(const LoraWanService &) = delete;
  LoraWanService &operator=(LoraWanService &&) = delete;
  ~LoraWanService() = default;

  /**
   * @brief Start the lorawan thread, this thread runs in it's own very low
   * priority to prevent the whole system from being blocked.
   */
  void start() {

    m_running.store(true, std::memory_order_release);

    // Spawn the thread.
    k_thread_create(&m_thread_data, m_stack, K_THREAD_STACK_SIZEOF(m_stack),
                    &LoraWanService::thread_entry, this, nullptr, nullptr,
                    K_LOWEST_APPLICATION_THREAD_PRIO, 0, K_NO_WAIT);

#ifdef CONFIG_ENABLE_FAULT_INJECTION
    m_fault_injection.init();
#endif
  }

  /**
   * @brief Stop the thread.
   */
  void stop() {

    m_running.store(false, std::memory_order_release);

    // Instantly wakes up thread if trapped in Phase 1
    k_sem_give(&m_join_sem);

    // Instantly unblocks k_msgq_get if stuck in Phase 2
    m_tx_q.purge();

    // Wait for thread death
    k_thread_join(&m_thread_data, K_FOREVER);
  }

  /**
   * @brief Returns true if we have connected to the LoRaWAN network.
   */
  [[nodiscard]] bool is_joined() const noexcept {
    return m_joined.load(std::memory_order_acquire);
  }

  /**
   * @brief Submit a sensor batch to be sent over LoRaWAN to the gateway.
   * @param [in] batch The sensor batch to send.
   * @param [in] term The current raft term.
   * @param [in] index The index of the raft log.
   * @param [in] alert Whether or not an alert has been triggered by the system.
   * @return true if the messaged was enqueued successfully.
   */
  template <typename SensorBatch>
  [[nodiscard]] bool
  send_batch(const SensorBatch &batch, const std::uint64_t term,
             const std::uint64_t index, const std::uint32_t seq) {

    Payload payload{};
    payload.m_node_id = common::get_eui64_as_uint64();
    payload.m_raft_term = static_cast<std::uint32_t>(term);
    payload.m_raft_log_index = static_cast<std::uint32_t>(index);
    payload.m_sequence = seq;
    payload.m_alert = batch.m_alert_active ? 1 : 0;
    payload.m_count = static_cast<std::uint8_t>(
        std::min(MAX_ENTRIES, std::size_t(batch.m_count)));

    for (std::uint8_t i = 0; i < payload.m_count; ++i) {
      payload.entries[i].m_device_rloc = batch.m_entries[i].m_eui;
      payload.entries[i].m_water_level_mm = batch.m_entries[i].m_water_level_mm;
      payload.entries[i].m_validity =
          static_cast<std::uint8_t>(batch.m_entries[i].m_validity);
      payload.entries[i].m_detail =
          static_cast<std::uint8_t>(batch.m_entries[i].m_detail);
      payload.entries[i].m_timestamp = batch.m_entries[i].m_timestamp;
    }

    return m_tx_q.try_put(payload);
  }

private:
  /**
   * @brief The main thread function. runs in two phases
   * @param [in out] p1 pointer to self.
   */
  static void thread_entry(void *self_ptr, void * /*p2*/, void * /*p3*/) {
    auto *self = static_cast<LoraWanService *>(self_ptr);

    logging::inf("LoraWanService: Thread started.");

    while (self->m_running.load()) {
      if (!self->is_joined()) {
        // PHASE 1: Attempt to join the network and back off progressively on
        // failed attempts.
        logging::inf("LoraWanService: Attempting to join network.");
        const bool join_ok =
            run_join(self->m_modem, {self->m_eui, self->m_app_key},
                     self->m_scratch) &&
            // Send a ping packet to set the correct data rate so we can send
            // our main packet
            self->send_ping();
        if (join_ok) {
          logging::inf("LoraWanService: Joined LoRaWAN network.");
          self->m_joined.store(true, std::memory_order_release);
          self->m_join_attempt = 0;
        } else {
          const std::uint32_t delay =
              Backoff::delay_for(self->m_join_attempt++);
          // Sleeps until backoff expires, or stop() kicks the semaphore early
          logging::inf("LoraWanService: Failed to join LoRaWAN network.");
          k_sem_take(&self->m_join_sem, common::ms_to_k_timeout(delay));
        }
      } else {
        // PHASE 2: Once joined, wait for data to be queue to send over
        // LoRaWAN.
        if (const auto payload = self->m_tx_q.get(K_FOREVER)) {
          bool success = self->ship_payload(payload.value());
          if (!success) {
            logging::err("LoraWanService: Failed to send message.");
          }
        }
      }
    }
  }

  /**
   * @brief A ping packet is used in order to set the data rate, before first
   * comms the data rate is set to only accept up 51 bytes. After sending a ping
   * LoRaWAN upgrades this connection.
   * @return true cmd executed okay.
   */
  bool send_ping() {
    return m_modem.ok(MSG_PING_CMD, MSG_DONE_RSP, Timeouts::DEFAULT_MS);
  }

  /**
   * @brief Handles sending a playload over LoRaWAN.
   * @param [in] payload The payload to send.
   * @return true if send was successful.
   */
  bool ship_payload(const Payload &payload) {

    // Turn out payload into AT MSG HEX.
    std::array<char, BUFFER_SIZE> cmd_buf{};
    const auto cmd = build_uplink(payload, cmd_buf);
    if (!cmd) {
      // Skip malformed payloads.
      logging::wrn("LoraWanService: malformed packet, dropping.");
      return false;
    }

    // Downlink data buffer.
    std::array<std::byte, BUFFER_SIZE> dl_buf{};
    std::size_t dl_len = 0;

    // Send the command.
    const CmdResult res =
        m_modem.cmd(*cmd, MSG_DONE_RSP, Timeouts::DEFAULT_MS, dl_buf, &dl_len);

    // Make sure we matched the expected result.
    if (res == CmdResult::MATCHED) {
      if (dl_len > 0) {
        handle_downlink({dl_buf.data(), dl_len});
      }

#ifdef CONFIG_ENABLE_FAULT_INJECTION
      // Pause between sending the message and telling egress the message has
      // been successfully sent for testing.
      const auto fault = m_fault_injection.get_fault();
      if (fault.m_pause_egress) {
        m_fault_injection.clear_fault();
        LOG_INF("FAULT:PAUSED_IN_WINDOW");
        k_msleep(fault.m_sleep_time_ms);
      }
#endif

      m_on_complete(payload.m_sequence, payload.m_raft_log_index);
      return true;
    }

    // If we get back that we are not joined, something has gone wrong, go back
    // to PHASE 1.
    if (res == CmdResult::NOT_JOINED) {
      m_joined.store(false, std::memory_order_release);
    }

    // Put the message back if the transmission failed so it isn't permanently
    // lost.
    (void)m_tx_q.try_put(payload);
    return false;
  }

  void handle_downlink(std::span<const std::byte> /*data*/) {
    // TODO: Handle any downlink data, config settings etc.
  }

  /**
   * @brief Semaphore for joining, while waiting to reattempt a join we wait on
   * this semaphore for either a timeout, or until stop wakes us.
   */
  k_sem m_join_sem{};

  /** @brief This nodes EUI-64 */
  std::array<std::uint8_t, common::EUI64_LEN> m_eui{};

  /** @brief The app key for joining the network/ */
  std::string_view m_app_key;

  /** @brief The number of joins attempted so far. */
  std::uint32_t m_join_attempt{0};

  /** @brief BUffer for join formatting. */
  std::array<char, SCRATCH_BUFFER_SIZE> m_scratch{};

  /** @brief atomic bool for if we are joined. */
  std::atomic<bool> m_joined{false};

  /** @brief The underlying modem for handling commands. */
  E5Modem m_modem;

  /** @brief Data queue for messages sent from the application.  */
  common::MessageQueue<Payload, TX_QUEUE_DEPTH> m_tx_q;

  /** @brief Atomic bool to indicate we are running. */
  std::atomic<bool> m_running{false};

  /** @brief The thread stack for the internal thread. */
  K_KERNEL_STACK_MEMBER(m_stack, 8192);

  /** @brief Thread struct for thread control. */
  struct k_thread m_thread_data;

  /** @brief Callback for when a message has been successfully sent. */
  stdext::inplace_function<void(std::uint32_t seq, std::uint64_t batch_index)>
      m_on_complete;

#ifdef CONFIG_ENABLE_FAULT_INJECTION
  fog::fault::FaultInjection m_fault_injection;
#endif
};

} // namespace fog::lora
