#pragma once

#include <openthread.h>
#include <zephyr/kernel.h>

namespace common {

/**
 * @brief Wraps the open thread mutex to use with scoped locks and lock guards.
 */
struct openthread_mutex {
  static void lock() { openthread_mutex_lock(); }
  static bool try_lock() { return openthread_mutex_try_lock() == 0; }
  static void unlock() { openthread_mutex_unlock(); }
};

struct adopt_lock_t {};

/**
 * @brief Custom mutex for wrapping a Zephyr k_mutex.
 * Follows the same pattern as std::mutex
 */
class mutex {
public:
  mutex() { k_mutex_init(&m_mutex); }
  void lock() { k_mutex_lock(&m_mutex, K_FOREVER); }
  bool try_lock() { return k_mutex_lock(&m_mutex, K_USEC(1)) == 0; }
  void unlock() { k_mutex_unlock(&m_mutex); }

private:
  struct k_mutex m_mutex;
};

} // namespace common
