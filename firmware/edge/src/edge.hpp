#pragma once

#include "common/alert.hpp"
#include "common/logging.hpp"
#include "common/ot_utils.hpp"
#include "common/sensor_reading.hpp"
#include "config/config.hpp"
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
    m_sensor_coap.init();
    m_alert_handler.init();
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
  /** @brief JSN-SR04T driver for communicating with sensor hardware. */
  jsn::Driver m_jsn_driver{PWR_PIN, TRIG_PIN, ECHO_PIN};

  /** @brief The main sensor task, loops every sleep_interval_s. Initialise
   * callbacks inline. */
  sensor::SensorCycle m_sensor_cycle{
      m_jsn_driver, sensor::SensorCycleParams{
                        .sleep_interval_ms =
                            [this] {
                              return m_alert.m_alert_active
                                         ? m_alert.m_alert_interval_ms
                                         : config::SLEEP_MS;
                            },
                        .emit_reading =
                            [this](const bool valid, const std::uint16_t raw_mm,
                                   const std::uint16_t ground_dist) {
                              send_sensor_reading(valid, raw_mm, ground_dist);
                            }}};

  /** @brief CoAP service for sending sensor data back to the fog node. */
  sensor::CoapService m_sensor_coap{common::MESH_LOCAL_MULTICAST_ADDR,
                                    common::SENSOR_URI};

  /** @brief Handle an alert message from the fog layer. */
  common::Alert m_alert_handler{[this](const auto alert) { m_alert = alert; }};

  /** @brief stored alert packet. */
  common::Alert::AlertPacket m_alert;
};

} // namespace edge
