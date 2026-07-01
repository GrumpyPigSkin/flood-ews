#pragma once

#include "common/ot_utils.hpp"
#include "common/sensor_reading.hpp"
#include "lorawan/service.hpp"
#include "lorawan/uart_e5.hpp"
#include "outlier_vote/engine.hpp"
#include "outlier_vote/service.hpp"
#include "raft/engine.hpp"
#include "sensor/service.hpp"

namespace fog {

class Application {

  static constexpr std::string_view LORAWAN_APP_KEY =
      "19305880A5D620295373F6BC388F4AA1";

public:
  void init() {
    m_uart.init();
    m_sensor_service.init();
  }

  void start() {
    m_lora_service.start();
    m_vote_service.start();
    m_raft_engine.start();
  }

  void stop() { m_lora_service.stop(); }

private:
  void on_apply(const raft::Server<>::EntryT &entry) {

    if (!m_raft_engine.is_leader()) {
      return;
    }

    vote::SensorBatch batch;
    if (entry.m_data_len < sizeof(batch)) {
      logging::err("entry idx=%llu too small ({} < {}), skipping",
                   entry.m_index, entry.m_data_len);
      return;
    }
    std::memcpy(&batch, &entry.m_data, sizeof(batch));

    logging::inf("on_apply: batch sent to LoRaWAN.");

    (void)m_lora_service.send_batch(batch, entry.m_term, entry.m_index, false);
  }

  common::Eui64Arr m_eui = common::get_eui64_as_arr8();
  fog::lora::E5Uart m_uart{DEVICE_DT_GET(DT_NODELABEL(uart2))};
  lora::LoraWanService m_lora_service{lora::Platform{
      .uart = m_uart.port(), .eui = m_eui, .app_key = LORAWAN_APP_KEY}};

  raft::Engine m_raft_engine{[this](const auto &e) { on_apply(e); },
                             common::get_eui64_as_uint64()};

  vote::VoteService m_vote_service{[this](const vote::SensorBatch &batch) {
    m_raft_engine.submit(
        std::span(std::bit_cast<const std::byte *>(&batch), sizeof(batch)));
  }};

  sensor::Service m_sensor_service{
      common::SENSOR_URI, [this](const common::SensorReadingWire &reading) {
        m_vote_service.accumulate(reading);
      }};
};

} // namespace fog
