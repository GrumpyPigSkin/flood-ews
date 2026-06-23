#pragma once

#include "zephyr/kernel.h"
#include <functional>

/**
 * @brief Wraps up submitting work to the global work queue.
 */
class WorkTask {
public:
  using FuncTypeT = std::function<void()>;
  /**
   * @brief Construct a new Work Task object
   * @param [in] fn The functor to be called when
   */
  explicit WorkTask(FuncTypeT fn) : m_fn(std::move(fn)) {
    k_work_init(&m_work, &WorkTask::trampoline);
  }

  /** @brief Delete copy/move constructors. */
  WorkTask(const WorkTask &) = delete;
  WorkTask &operator=(const WorkTask &) = delete;
  WorkTask(WorkTask &&) = delete;
  WorkTask &operator=(WorkTask &&) = delete;
  ~WorkTask() = default;

  /**
   * @brief Submit work to run on the global queue.
   */
  void submit() { k_work_submit(&m_work); }

  /**
   * @brief Submit work on the given queue q.
   * @param [in] q The queue to run the work on.
   */
  void submit_to(k_work_q &q) { k_work_submit_to_queue(&q, &m_work); }

private:
  /**
   * @brief Zephyr needs a static function to call into. From here we recover
   * the this pointer and call the callback.
   *
   * @param w
   */
  static void trampoline(k_work *const w) {
    CONTAINER_OF(w, WorkTask, m_work)->m_fn();
  }

  /**
   * @brief The Zephyr work object.
   */
  k_work m_work;

  /**
   * @brief User supplied functor to call.
   */
  FuncTypeT m_fn;
};
