#pragma once

#include "common/inplace_function.hpp"
#include "raft_types.hpp"
#include <cstdint>

/**
 * @brief Rather than binding the raft server to kernel API and calls, I inject
 * these dependencies with makes the server easier to test.
 */

namespace fog::raft {

/**
 * @brief Detail to indicate this is not part of the main API.
 */
namespace detail {

/**
 * @brief Check of an stdext::inplace_function is bound.
 * @param [in] func The stdext::inplace_function to check.
 * @return true if it is.
 * @return false
 */
template <typename Func> [[nodiscard]] bool bound(const Func &func) noexcept {
  return static_cast<bool>(func);
}

/**
 * @brief Invoke a function only if it is bound.
 * @param [in] func The function to call.
 * @param [in] args The args for the function.
 * @return true if it was called.
 */
template <typename Func, typename... Args>
bool call_if(const Func &func, Args &&...args) {
  if (bound(func)) {
    func(static_cast<Args &&>(args)...);
    return true;
  }
  return false;
}

} // namespace detail

/**
 * @brief Hooks supplied by the rest of the application. Templated on Cfg so
 * message/entry sizes line up with the Server.
 */
template <typename Cfg> struct Callbacks {

  /**
   * @brief Send a message to a single peer.
   */
  stdext::inplace_function<void(const Message<Cfg> &)> m_send{};

  /**
   * @brief Apply a committed entry, in log order, exactly once per index (5.3).
   */
  stdext::inplace_function<void(const Entry<Cfg> &)> m_apply{};

  /**
   * @brief Persist currentTerm and votedFor together (called on change).
   */
  stdext::inplace_function<void(Term current_term, NodeId voted_for)>
      m_persist_state{};

  /**
   * @brief Persist a single appended log entry.
   */
  stdext::inplace_function<void(const Entry<Cfg> &)> m_persist_log_append{};

  /**
   * @brief Truncate persisted log: delete entries from from_index onward.
   */
  stdext::inplace_function<void(Index from_index)> m_persist_log_truncate{};

  /**
   * @brief Delete persisted entries up to and including up_to_index.
   */
  stdext::inplace_function<void(Index up_to_index)> m_persist_log_compact{};

  /**
   * @brief Serialise the state machine into buf. Return bytes written, or a
   * negative value if it does not fit.
   */
  stdext::inplace_function<std::int32_t(std::uint8_t *buf, std::uint32_t cap)>
      m_snapshot_save{};

  /**
   * @brief Load the state machine from a fully received snapshot buffer.
   */
  stdext::inplace_function<void(const std::uint8_t *buf, std::uint32_t len)>
      m_snapshot_load{};

  /**
   * @brief MADATORY: Should return monotonic millisecond clock.
   */
  stdext::inplace_function<Time()> m_now{}; // monotonic millisecond clock

  /**
   * @brief MADATORY: Should return jitter for election timeout.
   */
  stdext::inplace_function<std::uint32_t()> m_rand{};

  /**
   * @brief Observability callback, called when the state of the server changes.
   */
  stdext::inplace_function<void(State old_state, State new_state)>
      m_on_state_change{};

  /**
   * @brief Check the mandatory functions are bound.
   * @return true if they are.
   */
  [[nodiscard]] bool valid() const noexcept {
    return detail::bound(m_now) && detail::bound(m_rand);
  }
};

} // namespace fog::raft
