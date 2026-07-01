#pragma once

#include <optional>
#include <type_traits>
#include <zephyr/kernel.h>

namespace common {

/**
 * @brief Fixed-capacity, statically-allocated, thread/ISR-safe message queue.
 * Wraps k_msgq with copy semantics. T must be trivially copyable since the
 * kernel moves items by memcpy.
 */
template <typename T, std::size_t Depth> class MessageQueue {
  static_assert(std::is_trivially_copyable_v<T>,
                "k_msgq copies by memcpy; T must be trivially copyable");
  static_assert(Depth > 0, "queue needs capacity");

public:
  /**
   * @brief Constructor
   */
  MessageQueue() { k_msgq_init(&m_q, m_buf, sizeof(T), Depth); }

  /** @brief Deleted copy/move constructors. */
  MessageQueue(const MessageQueue &) = delete;
  MessageQueue &operator=(const MessageQueue &) = delete;
  MessageQueue(MessageQueue &&) = delete;
  MessageQueue &operator=(MessageQueue &&) = delete;

  /**
   * @brief Non-blocking put, returns false if full.
   * @param [in] item
   * @return true If the item was successfully added.
   */
  [[nodiscard]] bool try_put(const T &item) {
    return k_msgq_put(&m_q, &item, K_NO_WAIT) == 0;
  }

  /**
   * @brief Try and put an item into the queue, blocks for timeout amount of
   * time.
   * @param [in] item The item to push.
   * @param [in] timeout The timeout.
   * @return true If it was successfully pushed.
   */
  [[nodiscard]] bool put(const T &item, const k_timeout_t timeout) {
    return k_msgq_put(&m_q, &item, timeout) == 0;
  }

  /**
   * @brief Try and pop and item of the queue return std::nullopt if no item is
   * available.
   * @return std::optional<T>
   */
  [[nodiscard]] std::optional<T> try_get() {
    T item;
    if (k_msgq_get(&m_q, &item, K_NO_WAIT) == 0) {
      return item;
    }
    return std::nullopt;
  }

  /**
   * @brief Pop an item off the queue, blocking up to the specified timeout.
   * @param [in] timeout The maximum time to wait for an item.
   * @return std::optional<T> The item if popped, or std::nullopt on timeout.
   */
  [[nodiscard]] std::optional<T> get(const k_timeout_t timeout) {
    T item;
    if (k_msgq_get(&m_q, &item, timeout) == 0) {
      return item;
    }
    return std::nullopt;
  }

  /**
   * @brief Drain every queued item through fn, on the calling context.
   */
  template <typename Fn> void drain(Fn &&fn) {
    T item;
    while (k_msgq_get(&m_q, &item, K_NO_WAIT) == 0) {
      fn(item);
    }
  }

  /**
   * @brief Get the number of items in the queue.
   * @return std::size_t
   */
  [[nodiscard]] std::size_t count() { return k_msgq_num_used_get(&m_q); }

  /**
   * @brief Purge all items from the queue.
   */
  void purge() { k_msgq_purge(&m_q); }

private:
  /**
   * @brief The queue.
   */
  k_msgq m_q{};

  /** @brief Queue storage aligned to T. */
  alignas(T) char m_buf[Depth * sizeof(T)];
};

} // namespace common
