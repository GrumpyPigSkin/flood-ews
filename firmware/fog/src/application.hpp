#pragma once

#include "common/ot_utils.hpp"
#include "lorawan/service.hpp"
#include "lorawan/uart_e5.hpp"
#include "raft/engine.hpp"
#include "sensor/service.hpp"

namespace fog {

class Application {

  static constexpr std::string_view LORAWAN_APP_KEY =
      "19305880A5D620295373F6BC388F4AA1";

public:
  void init() { m_uart.init(); }

  void start() { m_lora_service.start(); }

  void stop() { m_lora_service.stop(); }

private:
  common::Eui64Arr m_eui = common::get_eui64_as_arr8();
  fog::lora::E5Uart m_uart{DEVICE_DT_GET(DT_NODELABEL(uart2))};
  lora::LoraWanService m_lora_service{lora::Platform{
      .uart = m_uart.port(), .eui = m_eui, .app_key = LORAWAN_APP_KEY}};
  // raft::Engine m_raft_engine;
  // sensor::Service m_sensor_service{};
};

} // namespace fog
