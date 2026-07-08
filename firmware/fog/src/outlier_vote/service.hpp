#pragma once

#include "common/logging.hpp"
#include "common/mutex.hpp"
#include "common/periodic_task.hpp"
#include "common/sensor_reading.hpp"
#include "outlier_vote/engine.hpp"
#include "outlier_vote/sensor_batch.hpp"
#include <cstdint>
#include <functional>
#include <mutex>

namespace fog::vote {

/**
 * @brief Vote service, orchestrates the collecting and voting on accumulated
 * sensor values.
 */
class VoteService {
public:
  using Engine = VoteEngine;
  using Batch = batch::SensorBatch;
  using Entry = common::SensorReadingWire;
  using SubmitFn = std::function<void(const Batch &)>;
  using NowMsFn = std::function<std::optional<std::uint64_t>()>;

  /**
   * @brief Constructor
   * @param [in] submit Called with each new sensor batch.
   * @param [in] cfg The configuration for the underlying engine.
   */
  explicit VoteService(SubmitFn submit, NowMsFn now_ms, Config cfg = {})
      : m_engine{cfg}, m_submit{std::move(submit)},
        m_window([this] { on_window_close(); }), m_now_ms{std::move(now_ms)} {}

  /** @brief Deleted copy and move constructors. */
  VoteService(const VoteService &) = delete;
  VoteService &operator=(const VoteService &) = delete;
  VoteService(VoteService &&) = delete;
  VoteService &operator=(VoteService &&) = delete;
  ~VoteService() = default;

  /**
   * @brief Arm the first collection window.
   */
  void start() {
    const auto cfg = config();
    m_window.one_shot(next_boundary_timeout(cfg.m_collection_window_ms));
  }

  /**
   * @brief Stop the collection windown timer.
   */
  void stop() { m_window.stop(); }

  /**
   * @brief Force the window timer to re-align to the next boundary using
   * the current clock reading.
   */
  void resync() {
    const std::scoped_lock guard(m_lock);
    m_window.stop();
    m_window.one_shot(
        next_boundary_timeout(m_engine.config().m_collection_window_ms));
  }

  /**
   * @brief (Re)open the collection window, called when the leader signals a
   * window start. Restarts the timer, opening a fresh window. Safe from the RX
   * task.
   */
  void window_start() {
    const std::scoped_lock guard(m_lock);
    m_engine.open_window();
    m_window.one_shot(
        next_boundary_timeout(m_engine.config().m_collection_window_ms));
  }

  /**
   * @brief Feed one sensor reading. Safe from the RX task.
   */
  void accumulate(const Entry &entry) {
    const std::scoped_lock guard(m_lock);
    if (!m_engine.accumulate(entry)) {
      logging::err("accumulate reading: .eui={:x}", entry.m_eui);
    }
  }

  /**
   * @brief Set a new configuration.
   * @param cfg
   */
  void set_config(const Config &cfg) {
    const std::scoped_lock guard(m_lock);
    m_engine.set_config(cfg);
  }

  /**
   * @brief Get the current configuration.
   * @return Config
   */
  [[nodiscard]] Config config() const {
    const std::scoped_lock guard(m_lock);
    return m_engine.config();
  }

  /**
   * @brief Is an alert active.
   * @return true if it is.
   */
  [[nodiscard]] bool is_alert_active() const {
    const std::scoped_lock guard(m_lock);
    return m_engine.alert_active();
  }

  /**
   * @brief Set a new collection window for sampling.
   * @param [in] collection_window
   */
  void set_collection_window(const std::uint32_t collection_window) {
    const std::scoped_lock guard(m_lock);
    m_engine.set_collection_window(collection_window);
  }

  /**
   * @brief Copy the reputation into the span `out`.
   * @param [out] out user supplied buffer.
   * @return std::size_t the number written.
   */
  std::size_t reputation_snapshot(std::span<Reputation> out) const {
    const std::scoped_lock guard(m_lock);
    return m_engine.reputation_snapshot(out);
  }

private:
  /**
   * @brief Close the window, submit if a batch resulted, re-arm.
   * This is computed under a lock so any accumulate calls will wait for the
   * next batch.
   */
  void on_window_close() {
    std::optional<Batch> batch;
    std::uint32_t next_ms = 0;

    logging::inf("on_window_close: Sensor window closed.");
    {
      const std::scoped_lock guard(m_lock);
      batch = m_engine.close_window();
      next_ms = m_engine.config().m_collection_window_ms;
    }

    if (batch.has_value() && m_submit) {

      logging::inf("on_window_close: Batch submitted to raft.");
      batch->m_alert_active = m_engine.check_alert(batch.value());

      m_submit(batch.value());
    }

    m_window.one_shot(next_boundary_timeout(next_ms));
  }

  /**
   * @brief k_timeout_t until the next aligned period boundary, computed
   * fresh from the synchronised clock each call.
   * @param [in] period_ms Period; treated as 1 if 0 to avoid div-by-zero.
   */
  [[nodiscard]] k_timeout_t
  next_boundary_timeout(const std::uint32_t period_ms) const {
    const std::uint64_t period = period_ms == 0 ? 1 : period_ms;
    const std::optional<std::uint64_t> now = m_now_ms();

    if (!now.has_value()) {
      // No synchronised clock, fallback to unsynced.
      return common::ms_to_k_timeout(static_cast<std::uint32_t>(period));
    }

    const std::uint64_t next_boundary = ((now.value() / period) + 1) * period;
    const std::uint64_t delay_ms = next_boundary - now.value();
    return common::ms_to_k_timeout(static_cast<std::uint32_t>(delay_ms));
  }

  /** @brief The underlying logic. */
  Engine m_engine;

  /** @brief Submit function, called on submit. */
  SubmitFn m_submit;

  /** @brief Lock to guard against concurrent access. */
  mutable common::mutex m_lock;

  /** @brief Periodic task to work off timer. */
  common::PeriodicTask m_window;

  /** @brief Get the time now in ms. */
  NowMsFn m_now_ms;
};

} // namespace fog::vote
