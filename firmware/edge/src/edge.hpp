#pragma once

#include "common/coap_utils.hpp"
#include "common/logging.hpp"
#include "common/sensor_reading.hpp"
#include "config/service.hpp"
#include "jsn/driver.hpp"
#include "jsn/jsn_logic.hpp"
#include "sensor/sensor_coap.hpp"
#include "sensor/sensor_cycle.hpp"
#include <cstdint>

namespace edge {

/**
 * @brief The main application, wires up the components and initialises them.
 */
class Edge {

  /**
   * @brief Fetch GPIO specs from Devicetree overlay, used for the physical pins
   * on the dev kit.
   */
  static constexpr struct gpio_dt_spec PWR_PIN =
      GPIO_DT_SPEC_GET(DT_NODELABEL(jsn_pwr), gpios);
  static constexpr struct gpio_dt_spec TRIG_PIN =
      GPIO_DT_SPEC_GET(DT_NODELABEL(jsn_trig), gpios);
  static constexpr struct gpio_dt_spec ECHO_PIN =
      GPIO_DT_SPEC_GET(DT_NODELABEL(jsn_echo), gpios);

  static constexpr std::uint32_t S_TO_MS = 1000;

public:
  /**
   * @brief Initialise the application.
   */
  void init() {
    m_config_service.init();
    if (!m_jsn_driver.init()) {
      logging::err("Failed to start the sensor, aborting app.");
    }
    m_sensor_cycle.start();
  }

  /**
   * @brief The sensor callback, calculate the validity and send the data onto
   * CoAP.
   * @param valid
   * @param raw_mm
   * @param ground_dist
   */
  void send_sensor_reading(const bool valid, const std::uint16_t raw_mm,
                           const std::uint16_t ground_dist) {
    logging::inf("New reading: .is_ready={}, .raw_mm={}, .ground_mm={}", valid,
                 raw_mm, ground_dist);
    const auto reading = jsn::calculate_reading(valid, raw_mm, ground_dist);
    (void)m_sensor_coap.send_sensor_data(reading);
  }

private:
  /** @brief Configuration service for handling config updates. */
  config::ConfigService m_config_service{
      [this] { m_sensor_cycle.reschedule(); }, common::SENSOR_CONFIG_URI};

  /** @brief JSN-SR04T driver for communicating with sensor hardware. */
  jsn::Driver m_jsn_driver{PWR_PIN, TRIG_PIN, ECHO_PIN};

  /** @brief The main sensor task, loops every sleep_interval_s. Initialise
   * callbacks inline. */
  sensor::SensorCycle m_sensor_cycle{
      m_jsn_driver,
      sensor::SensorCycleParams{
          .sleep_interval_ms =
              [this] { return m_config_service.sleep_interval_s() * S_TO_MS; },
          .warmup_ms = [this] { return m_config_service.sensor_warmup_ms(); },
          .timeout_ms = [this] { return m_config_service.sensor_timeout_ms(); },
          .ground_distance_mm =
              [this] { return m_config_service.ground_distance_mm(); },
          .emit_reading =
              [this](const bool valid, const std::uint16_t raw_mm,
                     const std::uint16_t ground_dist) {
                send_sensor_reading(valid, raw_mm, ground_dist);
              }}};

  /** @brief CoAP service for sending sensor data back to the fog node. */
  sensor::CoapService m_sensor_coap{coap_utils::MESH_LOCAL_MULTICAST_ADDR,
                                    common::SENSOR_URI};
};

} // namespace edge
