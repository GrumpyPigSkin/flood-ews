#pragma once

#include "common/alert.hpp"
#include "common/global_config.h"
#include "common/logging.hpp"
#include "common/ot_utils.hpp"
#include "common/security/trusted_device_store.hpp"
#include "common/security/trusted_devices.hpp"
#include "common/sensor_reading.hpp"
#include "egress/egress_coordinator.hpp"
#include "lorawan/service.hpp"
#include "lorawan/uart_e5.hpp"
#include "outlier_vote/engine.hpp"
#include "outlier_vote/sensor_batch.hpp"
#include "outlier_vote/service.hpp"
#include "raft/engine.hpp"
#include "raft/raft_types.hpp"
#include "secrets/provisioned_keys.hpp"
#include "sensor/service.hpp"
#include "utils/time_sync.h"
#include "zephyr/kernel.h"
#include <cassert>
#include <type_traits>

namespace fog {

/**
 * @brief The main application.
 */
class Application {

public:
  /**
   * @brief Initialise all sub components.
   */
  void init() {
    const auto eui = common::get_eui64_as_uint64();
    if (const auto err = m_trusted_device_store.init(eui); err != PSA_SUCCESS) {
      logging::err("Failed to initialise trust store.");
      return;
    }

    if (const auto err = m_trusted_device_store.trust_peers(TRUSTED_DEVICES);
        err != PSA_SUCCESS) {
      logging::err("Failed to initialise trust store.");
      return;
    }

    const auto pub_key = m_trusted_device_store.export_pubkey();
    if (pub_key.has_value()) {
      logging::inf(".eui={:#x}, .pub_key={::#x}", common::get_eui64_as_uint64(),
                   pub_key.value());
    }

    m_uart.init();
    m_sensor_service.init();
    m_raft_engine.init();
    m_alert_service.init();
  }

  /**
   * @brief Start all sub components.
   */
  void start() {
    m_lora_service.start();
    m_vote_service.start();
    m_raft_engine.start();
  }

  /** @brief Stop the LoRaWAN thread. */
  void stop() { m_lora_service.stop(); }

  static constexpr std::uint16_t ALERT_INTERVAL_MS = common::ALERT_TIMEOUT_MS;

private:
  /**
   * @brief Called by raft when it applies.
   * @param [in] entry
   */
  void on_apply(const raft::Server<>::EntryT &entry) {

    if (entry.m_type == raft::EntryType::EGRESS_MSG) {
      egress::EgressRecord egr;
      if (entry.m_data_len == sizeof(egr)) {
        std::memcpy(&egr, &entry.m_data, sizeof(egr));
        m_egress_coordinator.on_egress_applied(egr);
      }
    } else if (entry.m_type == raft::EntryType::SENSOR_DATA) {
      // On submit check for an alert and sens immediately.
      check_alert(entry);
      // The egress coordinator will start the message to the gateway.
      m_egress_coordinator.begin_egress(entry.m_index);
    }
  }

  /**
   * @brief Check the entry for an alert and emit the alert to all peers.
   * @param [in] entry The entry
   */
  void check_alert(const raft::Server<>::EntryT &entry) {
    assert(entry.m_type == raft::EntryType::SENSOR_DATA);

    batch::SensorBatch batch;
    std::memcpy(&batch, entry.m_data.data(), sizeof(batch));

    // State hasn't changed, nothing to do.
    if (batch.m_alert_active == m_alert_active) {
      return;
    }

    m_alert_active = batch.m_alert_active;
    const auto eui = common::get_eui64_as_uint64();

    if (m_alert_active) {
      m_alert_service.send_alert(eui, {.m_alert_interval_ms = ALERT_INTERVAL_MS,
                                       .m_alert_active = true});
      m_vote_service.set_collection_window(ALERT_INTERVAL_MS);
    } else {
      m_alert_service.send_alert(
          eui, {.m_alert_interval_ms = 0, .m_alert_active = false});
      m_vote_service.set_collection_window(
          vote::OutlierVoteConfig::m_collection_window_ms);
    }

    m_vote_service.resync();
  }

  /**
   * @brief Configure the hooks for the egress coordinator.
   * @return egress::Hooks<raft::Server<>::EntryT, batch::SensorBatch>
   */
  egress::Hooks<raft::Server<>::EntryT, batch::SensorBatch>
  make_egress_hooks() {
    return {.m_is_leader = [this] { return m_raft_engine.is_leader(); },
            // Submit an egress record to raft.
            .m_submit_egress =
                [this](const egress::EgressRecord &rec) {
                  return m_raft_engine
                      .submit(raft::EntryType::EGRESS_MSG,
                              std::span(std::bit_cast<const std::byte *>(&rec),
                                        sizeof(rec)))
                      .has_value();
                },
            // Read an entry from raft
            .m_read_entry =
                [this](const std::uint64_t index) {
                  return m_raft_engine.get_entry(index);
                },
            // Submit the batch to LoRaWAN
            .m_lorawan_send =
                [this](const batch::SensorBatch &batch, std::uint64_t term,
                       std::uint64_t index, std::uint32_t seq) {
                  (void)m_lora_service.send_batch(batch, term, index, seq);
                }};
  }

  common::Eui64Arr m_eui = common::get_eui64_as_arr8();
  fog::lora::E5Uart m_uart{DEVICE_DT_GET(DT_NODELABEL(uart2))};
  lora::LoraWanService m_lora_service{lora::Platform{
      .uart = m_uart.port(),
      .eui = m_eui,
      .app_key = LORAWAN_APP_KEY,
      .m_on_complete = [this](std::uint32_t seq, std::uint64_t batch_index) {
        m_egress_coordinator.on_lorawan_ok(seq, batch_index);
      }}};

  /** @brief Raft configuration. */
  raft::Engine m_raft_engine{
      [this](const auto &entry) { on_apply(entry); },

      // State change.
      [this](const raft::State old_state, const raft::State new_state) {
        logging::inf("State changed from: {} to: {}", to_string(old_state),
                     to_string(new_state));

        if (new_state == raft::State::LEADER) {
#ifdef CONFIG_ENABLE_FAULT_INJECTION
          logging::inf("RAFT:BECAME_LEADER");
#endif
          m_egress_coordinator.on_became_leader();
        }
#ifdef CONFIG_ENABLE_FAULT_INJECTION
        else if (new_state == raft::State::FOLLOWER) {
          logging::inf("RAFT:BECAME_FOLLOWER");
        }
#endif
      },
      common::get_eui64_as_uint64(), m_trusted_device_store};

  /** @brief Outlier vote service. */
  vote::VoteService m_vote_service{
      // Called when a new back has been received by the outlier vote layer.
      [this](const batch::SensorBatch &batch) {
        if (const auto err = m_raft_engine.submit(
                raft::EntryType::SENSOR_DATA,
                std::span(std::bit_cast<const std::byte *>(&batch),
                          sizeof(batch)));
            !err.has_value()) {
          logging::err(
              "Submit failed: .err={}",
              static_cast<std::underlying_type_t<raft::Error>>(err.error()));
        }
      },
      // Get the current time.
      [] -> std::optional<std::uint64_t> {
        if (const auto time_us = time_sync::get_time_us()) {
          return time_us.value() / 1000;
        }
        return k_uptime_seconds() * 1000;
      }};

  /** @brief Sensor service for collecting readings */
  sensor::Service m_sensor_service{
      common::SENSOR_URI,
      [this](const common::SensorReadingWire &reading) {
        m_vote_service.accumulate(reading);
      },
      m_trusted_device_store};

  /** @brief Handling replicating egress status amongst fellow fog nodes. */
  egress::EgressCoordinator<raft::Server<>::EntryT, batch::SensorBatch>
      m_egress_coordinator{make_egress_hooks()};

  /** @brief Handle receiving alert messages.. */
  common::Alert m_alert_service{[this](const auto alert) {
    // Ignore if we sent the message.
    if (m_raft_engine.is_leader()) {
      return;
    }

    if (alert.m_alert_active) {
      // Set the alert time and resync the timeout window.
      logging::inf("Received an active alert, changing period to: {} s",
                   alert.m_alert_interval_ms);
      auto alert_ms = alert.m_alert_interval_ms;
      m_vote_service.set_collection_window(alert_ms);
      m_vote_service.resync();
    } else {
      // Reset the alert time.
      logging::inf("Alert cancelled", alert.m_alert_interval_ms);
      m_vote_service.set_collection_window(
          vote::OutlierVoteConfig::m_collection_window_ms);
      m_vote_service.resync();
    }
  }};

  /** @brief Trusted devices for signing messages. */
  common::TrustedDeviceStore m_trusted_device_store;

  /** @brief Is an alert active. */
  bool m_alert_active;
};

} // namespace fog
