#pragma once

#include "common/ot_utils.hpp"
#include "common/sensor_reading.hpp"
#include "egress/egress_coordinator.hpp"
#include "lorawan/service.hpp"
#include "lorawan/uart_e5.hpp"
#include "outlier_vote/sensor_batch.hpp"
#include "outlier_vote/service.hpp"
#include "raft/engine.hpp"
#include "raft/raft_types.hpp"
#include "sensor/service.hpp"

namespace fog {

class Application {

  static constexpr std::string_view LORAWAN_APP_KEY =
      "19305880A5D620295373F6BC388F4AA1";

public:
  void init() {
    m_uart.init();
    m_sensor_service.init();
    m_raft_engine.init();
  }

  void start() {
    m_lora_service.start();
    m_vote_service.start();
    m_raft_engine.start();
  }

  void stop() { m_lora_service.stop(); }

private:
  void on_apply(const raft::Server<>::EntryT &entry) {
    if (entry.m_type == raft::EntryType::EGRESS_MSG) {
      egress::EgressRecord egr;
      if (entry.m_data_len == sizeof(egr)) {
        std::memcpy(&egr, &entry.m_data, sizeof(egr));
        m_egress_coordinator.on_egress_applied(egr);
      }
    }
    // The egress coordinator will start the message to the gateway.
    else if (entry.m_type == raft::EntryType::SENSOR_DATA) {
      m_egress_coordinator.begin_egress(entry.m_index);
    }
  }

  egress::Hooks<raft::Server<>::EntryT, batch::SensorBatch>
  make_egress_hooks() {
    return {.m_is_leader = [this] { return m_raft_engine.is_leader(); },
            .m_submit_egress =
                [this](const egress::EgressRecord &rec) {
                  return m_raft_engine
                      .submit(raft::EntryType::EGRESS_MSG,
                              std::span(std::bit_cast<const std::byte *>(&rec),
                                        sizeof(rec)))
                      .has_value();
                },
            .m_read_entry =
                [this](const std::uint64_t index) {
                  return m_raft_engine.get_entry(index);
                },
            .m_lorawan_send =
                [this](const batch::SensorBatch &batch, std::uint64_t term,
                       std::uint64_t index, std::uint32_t seq) {
                  (void)m_lora_service.send_batch(batch, term, index, seq);
                }

    };
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

  raft::Engine m_raft_engine{[this](const auto &entry) { on_apply(entry); },
                             common::get_eui64_as_uint64()};

  vote::VoteService m_vote_service{[this](const batch::SensorBatch &batch) {
    (void)m_raft_engine.submit(
        raft::EntryType::SENSOR_DATA,
        std::span(std::bit_cast<const std::byte *>(&batch), sizeof(batch)));
  }};

  sensor::Service m_sensor_service{
      common::SENSOR_URI, [this](const common::SensorReadingWire &reading) {
        m_vote_service.accumulate(reading);
      }};

  egress::EgressCoordinator<raft::Server<>::EntryT, batch::SensorBatch>
      m_egress_coordinator{make_egress_hooks()};
};

} // namespace fog
