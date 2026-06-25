#pragma once

#include "common/periodic_task.hpp"
#include "common/work_task.hpp"
#include "jsn/driver.hpp"
#include "jsn/jsn_logic.hpp"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <zephyr/kernel.h>

namespace edge::sensor {

/**
 * @brief Params for SensorCycle.
 */
struct SensorCycleParams {
  /** @brief Timings in ms from a config source. */
  std::function<std::uint32_t()> sleep_interval_ms;
  std::function<std::uint32_t()> warmup_ms;
  std::function<std::uint32_t()> timeout_ms;
  std::function<std::uint32_t()> ground_distance_mm;

  /**
   * @brief Callback fired when a new reading is produced.
   */
  std::function<void(bool is_ready, std::uint32_t raw_mm,
                     std::uint32_t ground_mm)>
      emit_reading;

  /**
   * @brief Time function defaulted to kernal uptime.
   */
  std::function<std::int64_t()> now_ms = [] { return k_uptime_get(); };
};

/**
 * @brief Driver cycle handles the full measurement lifecycle:
 * sleep -> power on -> warmup -> trigger (+arm timeout) -> echo done | timeout
 * -> read + send -> schedule next sleep
 */
class SensorCycle {
public:
  /**
   * @brief Constructor
   * @param [in] sensor Reference to the sensor to coordinate.
   * @param [in] deps Dependencies required to reduce coupling.
   */
  SensorCycle(jsn::Driver &sensor, SensorCycleParams deps)
      : m_jsn{sensor}, m_deps{std::move(deps)},
        m_sleep([this] { on_sleep_elapsed(); }),
        m_warmup([this] { on_warmup_elapsed(); }),
        m_timeout([this] { on_timeout_elapsed(); }),
        m_read_work([this] { do_read(); }) {
    // Driver completion -> stop timeout, run the reading work.
    m_jsn.on_done([this](const jsn::ReadingState) {
      m_timeout.stop();
      m_read_work.submit();
    });
  }

  /** @brief Copy/Move constructors and destructor. */
  SensorCycle(const SensorCycle &) = delete;
  SensorCycle &operator=(const SensorCycle &) = delete;
  SensorCycle(SensorCycle &&) = delete;
  SensorCycle &operator=(SensorCycle &&) = delete;
  ~SensorCycle() = default;

  /**
   * @brief Begin cycling. Aligns the first sample to one sleep interval from
   * now.
   */
  void start() {
    m_jsn.power_off();
    schedule_sleep_from(m_deps.now_ms());
  }

  /**
   * @brief Abort any in-flight cycle and realign to a fresh sleep interval.
   * Safe to call from the RX task when the router pushes new config / alert
   * state.
   */
  void reschedule() {
    abort();
    schedule_sleep_from(m_deps.now_ms());
  }

private:
  /**
   * @brief Stop everything and return to a known idle state.
   */
  void abort() {
    m_sleep.stop();
    m_warmup.stop();
    m_timeout.stop();
    m_jsn.power_off();
    m_jsn.reset();
  }

  /**
   * @brief Sleep timer has elapsed. Power up the sensor and start warmup.
   */
  void on_sleep_elapsed() {
    m_cycle_start_ms = m_deps.now_ms();
    m_jsn.power_on();
    m_warmup.one_shot(common::ms_to_k_timeout(m_deps.warmup_ms()));
  }

  /**
   * @brief Called when warmup timer elapses, trigger the sensor pulse and and
   * arm the timeout guard.
   */
  void on_warmup_elapsed() {
    m_jsn.trigger();
    m_timeout.one_shot(common::ms_to_k_timeout(m_deps.timeout_ms()));
  }

  /**
   * @brief Called when the timer elapses before a valid reading has been
   * produced.
   */
  void on_timeout_elapsed() { m_jsn.on_timeout(); }

  /**
   * @brief Read the sample, hand it over to be emitted, sleep until the next
   * period.
   */
  void do_read() {
    m_jsn.power_off();
    const auto [state, dist] = m_jsn.snapshot();
    const bool is_ready = (state == jsn::ReadingState::VALID);
    if (m_deps.emit_reading) {
      m_deps.emit_reading(is_ready, dist, m_deps.ground_distance_mm());
    }
    m_jsn.reset();
    schedule_sleep_from(m_cycle_start_ms);
  }

  /**
   * @brief Schedule the next sleep period. The next wake is one
   * full interval after `anchor`, minus whatever has already elapsed.
   * @param anchor The time when the cycle was started.
   */
  void schedule_sleep_from(const std::int64_t anchor) {
    const std::int64_t period = static_cast<std::int64_t>(
        m_deps.sleep_interval_ms ? m_deps.sleep_interval_ms() : 0);
    const std::int64_t elapsed = m_deps.now_ms() - anchor;
    std::int64_t remaining = period - elapsed;
    // overran -> fire immediately
    remaining = std::max<std::int64_t>(remaining, 0);
    m_sleep.one_shot(
        common::ms_to_k_timeout(static_cast<std::uint32_t>(remaining)));
  }

  /** @brief Reference to the sensor. */
  jsn::Driver &m_jsn;

  /** @brief External dependencies. */
  SensorCycleParams m_deps;

  /** @brief Main sleep timer. */
  common::PeriodicTask m_sleep;

  /** @brief Warmup timer after powering on. */
  common::PeriodicTask m_warmup;

  /** @brief Timeout against the echo taking too long. */
  common::PeriodicTask m_timeout;

  /** @brief Work for when a successful reading arrives.  */
  common::WorkTask m_read_work;

  /** @brief The start time on each cycle. */
  std::int64_t m_cycle_start_ms{0};
};

} // namespace edge::sensor
