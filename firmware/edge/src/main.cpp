
#include "common/coap_utils.hpp"
#include "common/logging.hpp"
#include "jsn/driver.hpp"
#include "sensor/sensor_cycle.hpp"
#include <memory>

namespace {
alignas(edge::jsn::Driver)
    std::array<std::byte, sizeof(edge::jsn::Driver)> g_sensor_storage;

alignas(edge::sensor::SensorCycle)
    std::array<std::byte, sizeof(edge::jsn::Driver)> g_sensor_cycle_storage;
} // namespace

namespace edge {

inline auto get_sensor() {
  return reinterpret_cast<jsn::Driver *>(g_sensor_storage.data());
}

inline auto get_sensor_cycle() {
  return reinterpret_cast<sensor::SensorCycle *>(g_sensor_cycle_storage.data());
}

} // namespace edge

/**
 * @brief Fetch GPIO specs from Devicetree overlay, used for the physical pins
 * on the dev kit.
 */
static const struct gpio_dt_spec s_pwr_pin =
    GPIO_DT_SPEC_GET(DT_NODELABEL(jsn_pwr), gpios);
static const struct gpio_dt_spec s_trig_pin =
    GPIO_DT_SPEC_GET(DT_NODELABEL(jsn_trig), gpios);
static const struct gpio_dt_spec s_echo_pin =
    GPIO_DT_SPEC_GET(DT_NODELABEL(jsn_echo), gpios);

int main(void) {

  edge::sensor::SensorCycleParams scp{
      .sleep_interval_ms = [] { return 1000; },
      .warmup_ms = [] { return 100; },
      .timeout_ms = [] { return 1000; },
      .ground_distance_mm = [] { return 2000; },
      .emit_reading =
          [](bool is_ready, std::uint32_t raw_mm, std::uint32_t ground_mm) {
            logging::inf("New reading: .is_ready={}, .raw_mm={}, .ground_mm={}",
                         is_ready, raw_mm, ground_mm);
          }};

  std::construct_at(edge::get_sensor(), s_pwr_pin, s_trig_pin, s_echo_pin);
  std::construct_at(edge::get_sensor_cycle(), *edge::get_sensor(),
                    std::move(scp));

  if (edge::get_sensor()->init()) {
    edge::get_sensor_cycle()->start();
  }
}
