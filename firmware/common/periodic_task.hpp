#pragma once

#include <functional>
#include <zephyr/kernel.h>

namespace logging {

/**
 * @brief Periodic task encapsulates running a timer task on the main Zephyr
 * thread. This is rather than each feature needing own it's own timer and
 * timing functionality.
 */
class PeriodicTask {
public:
  /**
   * @brief Construct a new periodic task.
   * @param fn The function for the timer to call.
   */
  explicit PeriodicTask(std::function<void()> fn) : m_fn(std::move(fn)) {
    k_timer_init(&m_timer, &PeriodicTask::timer_trampoline, nullptr);
    k_timer_user_data_set(&m_timer, this);
    k_work_init(&m_work, &PeriodicTask::work_trampoline);
  }

  /**
   * @brief Non copyable but is moveable.
   * Rule of 5 dictates all 5 copy/move/destructor functions should be named.
   */
  PeriodicTask(const PeriodicTask &) = delete;
  PeriodicTask &operator=(const PeriodicTask &) = delete;
  PeriodicTask(PeriodicTask &&) = delete;
  PeriodicTask &operator=(PeriodicTask &&) = delete;
  ~PeriodicTask() { k_timer_stop(&m_timer); }

  /**
   * @brief Start the timer with the given period.
   * @param [in] period The period to start the timer.
   */
  void start(const k_timeout_t period) {
    k_timer_start(&m_timer, period, period);
  }

  /**
   * @brief One shot the timer in period timer.
   * @param [in] period The period to run the timer.
   */
  void one_shot(const k_timeout_t period) {
    k_timer_start(&m_timer, period, K_NO_WAIT);
  }

  /**
   * @brief Stop the timer.
   */
  void stop() { k_timer_stop(&m_timer); }

private:
  /**
   * @brief Call back into the original instance that started the timer, trigger
   * the work to start on the thread.
   * @param t The timer instance.
   */
  static void timer_trampoline(k_timer *const t) {
    // ISR context: recover the object, defer to thread context.
    auto *self = static_cast<PeriodicTask *>(k_timer_user_data_get(t));
    k_work_submit(&self->m_work);
  }

  /**
   * @brief Derive the original function that should be called for the given
   * work w.
   * @param w The work that was called.
   */
  static void work_trampoline(k_work *const w) {
    // Thread context: recover the object via CONTAINER_OF, run the work.
    auto *self = CONTAINER_OF(w, PeriodicTask, m_work);
    self->m_fn();
  }

  /**
   * @brief Kernal timer to trigger the periodic work.
   */
  k_timer m_timer;

  /**
   * @brief Work object sent to the kernal to schedule work on timer timeout.
   */
  k_work m_work;

  /**
   * @brief The underlying function to be called inside of the work callback.
   */
  std::function<void()> m_fn;
};

} // namespace logging
