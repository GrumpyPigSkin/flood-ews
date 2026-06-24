#pragma once

#include "common/logging.hpp"
#include "jsn/jsn_logic.hpp"
#include <atomic>
#include <cstdint>
#include <functional>
#include <utility>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

namespace edge::jsn {

/**
 * @brief Driver for the JSN-SR04T sensor. This handles the interrupt for the
 * echo pin and the driving of the power and trigger pins.
 */
class Driver {

  /**
   * @brief Helper to store the state of the reading and the measurement.
   */
  struct ReadingPair {
    ReadingState state;
    std::uint16_t reading_mm;
  };

  /** @brief Hold time for the trigger pin in microseconds. */
  static constexpr std::uint8_t GPIO_HOLD_US = 10U;

public:
  /**
   * @brief Runs once per measurement, either on a reading or on timeout.
   * Called within ISR context.
   */
  using DoneFn = std::function<void(ReadingState)>;

  /**
   * @brief Constructor
   * @param [in] pwr The power pin from GPIO_DT_SPEC_GET(...)
   * @param [in] trig The trigger pin from GPIO_DT_SPEC_GET(...)
   * @param [in] echo The echo pin from GPIO_DT_SPEC_GET(...)
   */
  Driver(const gpio_dt_spec &pwr, const gpio_dt_spec &trig,
         const gpio_dt_spec &echo) noexcept
      : m_pwr{pwr}, m_trig{trig}, m_echo{echo} {}

  /** @brief Deleted copy and move constructors. */
  Driver(const Driver &) = delete;
  Driver &operator=(const Driver &) = delete;
  Driver(Driver &&) = delete;
  Driver &operator=(Driver &&) = delete;

  /**
   * @brief Initialise the GPIO on that is used to handle the JSN sensor.
   * @return true Initialisation was successful.
   */
  [[nodiscard]] bool init() noexcept {
    logging::inf("Init called");
    if (!gpio_is_ready_dt(&m_pwr) || !gpio_is_ready_dt(&m_trig) ||
        !gpio_is_ready_dt(&m_echo)) {
      return false;
    }

    gpio_pin_configure_dt(&m_pwr, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&m_trig, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&m_echo, GPIO_INPUT);

    m_cb_ctx.self = this;
    gpio_init_callback(&m_cb_ctx.cb, &Driver::edge_trampoline, BIT(m_echo.pin));
    return gpio_add_callback(m_echo.port, &m_cb_ctx.cb) == 0;
  }

  /**
   * @brief Power on the sensor.
   */
  void power_on() noexcept { gpio_pin_set_dt(&m_pwr, 1); }

  /**
   * @brief Power off the sensor.
   */
  void power_off() noexcept { gpio_pin_set_dt(&m_pwr, 0); }

  /**
   * @brief Register the completion callback.
   * @param The callback function fn.
   */
  void on_done(DoneFn cb_fun) noexcept { m_done = std::move(cb_fun); }

  /**
   * @brief Begin the measurement.
   * Arm the interrupt and send the 10 us trigger to start the sensor.
   */
  void trigger() noexcept {
    m_resolved.store(false, std::memory_order_relaxed);
    publish(ReadingState::IN_FLIGHT, 0);
    m_rising_cycles = 0;
    gpio_pin_interrupt_configure_dt(&m_echo, GPIO_INT_EDGE_BOTH);
    gpio_pin_set_dt(&m_trig, 1);
    // 10us trigger pulse.
    k_busy_wait(GPIO_HOLD_US);
    gpio_pin_set_dt(&m_trig, 0);
  }

  /**
   * @brief Called from a timer if no falling edge arrived. Disarms and marks
   * timeout.
   */
  void on_timeout() noexcept {
    if (m_resolved.exchange(true, std::memory_order_acq_rel)) {
      return;
    }
    gpio_pin_interrupt_configure_dt(&m_echo, GPIO_INT_DISABLE);
    publish(ReadingState::TIMEOUT, 0);
    if (m_done) {
      m_done(ReadingState::TIMEOUT);
    }
  }

  /**
   * @brief Reset the sensor state.
   */
  void reset() noexcept {
    m_resolved.store(false, std::memory_order_relaxed);
    publish(ReadingState::IDLE, 0);
  }

  /**
   * @brief Load the current state.
   * @return ReadingState
   */
  [[nodiscard]] ReadingState state() const noexcept {
    return load_reading().state;
  }

  /**
   * @brief Is the driver ready for another sample.
   * @return true if it is.
   */
  [[nodiscard]] bool is_ready() const noexcept {
    return state() == ReadingState::VALID;
  }

  /**
   * @brief Has the driver timed out waiting for a sample.
   * @return true if it has.
   */
  [[nodiscard]] bool is_timeout() const noexcept {
    return state() == ReadingState::TIMEOUT;
  }

  /**
   * @brief Returns the last reading in mm.
   * @return std::uint16_t
   */
  [[nodiscard]] std::uint16_t raw_distance_mm() const noexcept {
    return load_reading().reading_mm;
  }

  /**
   * @brief Read the state and distance together.
   * @return ReadingPair
   */
  [[nodiscard]] ReadingPair snapshot() const noexcept { return load_reading(); }

private:
  /**
   * @brief ISR context for handling the callback.
   */
  struct CbCtx {
    gpio_callback cb;
    Driver *self;
  };

  /**
   * @brief Static trampoline function to attach to the ISR.
   */
  static void edge_trampoline(const struct device * /*port*/,
                              struct gpio_callback *callback,
                              gpio_port_pins_t /*pins*/) {
    CONTAINER_OF(callback, CbCtx, cb)->self->on_edge();
  }

  /**
   * @brief Called on each edge of the JSN sensor gpio input.
   * We aren't told what edge is which so we need figure it out in sequence.
   */
  void on_edge() noexcept {

    const int level = gpio_pin_get_dt(&m_echo);
    const ReadingState state = load_reading().state;

    if (level == 1 && state == ReadingState::IN_FLIGHT) {
      // rising edge: timestamp the start of the echo pulse
      m_rising_cycles = k_cycle_get_32();
    } else if (level == 0 && state == ReadingState::IN_FLIGHT &&
               m_rising_cycles != 0U) {

      // falling edge: compute pulse width amd submit.
      const std::uint32_t falling = k_cycle_get_32();
      const std::uint32_t delta = falling - m_rising_cycles;
      const std::uint32_t reading_us = cyc_to_us_near32(delta);
      const std::uint32_t dist = distance_mm_from_us(reading_us);

      // First-wins vs a concurrent timeout. If timeout already claimed
      // it, drop this reading.
      if (m_resolved.exchange(true, std::memory_order_acq_rel)) {
        return;
      }

      gpio_pin_interrupt_configure_dt(&m_echo, GPIO_INT_DISABLE);

      publish(ReadingState::VALID, static_cast<std::uint16_t>(dist));

      if (m_done) {
        m_done(ReadingState::VALID);
      }
    }
  }

  /**
   * @brief The publishing and reading of data is sequence locked using an
   * atomic variable. This way we make sure all values are written before
   * signalling to the reader that it is okay to read them.
   * @param [in] state
   * @param [in] dist
   */
  void publish(ReadingState state, std::uint16_t dist) noexcept {
    const std::uint32_t seq = m_seq.load(std::memory_order_relaxed);
    m_seq.store(seq + 1, std::memory_order_release); // odd: write begins
    m_state.store(state, std::memory_order_relaxed);
    m_distance_mm.store(dist, std::memory_order_relaxed);
    m_seq.store(seq + 2, std::memory_order_release); // even: write done
  }

  /**
   * @brief Load the reading pair, this ensures that the whole reading is valid
   * before we try and consume it.
   * @return ReadingPair
   */
  [[nodiscard]] ReadingPair load_reading() const noexcept {
    for (;;) {
      const std::uint32_t seq1 = m_seq.load(std::memory_order_acquire);
      if ((seq1 & 1U) != 0U) {
        // writer mid-update, retry
        continue;
      }
      const ReadingState state = m_state.load(std::memory_order_relaxed);
      const std::uint16_t dist = m_distance_mm.load(std::memory_order_relaxed);
      const std::uint32_t seq2 = m_seq.load(std::memory_order_acquire);
      if (seq1 == seq2) {
        return {state, dist}; // stable across the read
      }
    }
  }

  /** @brief The power pin. */
  gpio_dt_spec m_pwr;

  /** @brief The trigger pin. */
  gpio_dt_spec m_trig;

  /** @brief The power echo. */
  gpio_dt_spec m_echo;

  /** @brief Context for the callback. */
  CbCtx m_cb_ctx{};

  /** @brief Cycle count on a rising edge. */
  std::uint32_t m_rising_cycles{0};

  /** @brief First-wins arbitration between echo and timeout. */
  std::atomic<bool> m_resolved{false};

  /** @brief Callback called when done. */
  DoneFn m_done;

  /** @brief Seqlock-protected published reading. */
  std::atomic<std::uint32_t> m_seq{0};

  /** @brief Current sensor state. */
  std::atomic<ReadingState> m_state{ReadingState::IDLE};

  /** @brief Distance on last reading. */
  std::atomic<std::uint16_t> m_distance_mm{0};
};

} // namespace edge::jsn
