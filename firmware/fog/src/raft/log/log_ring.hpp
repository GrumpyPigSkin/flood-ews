#pragma once

#include "raft/raft_types.hpp"
#include <array>
#include <cstdint>
#include <optional>

namespace fog::raft {

/**
 * @brief Ring buffer for the Raft log.
 * @tparam Cfg
 */
template <typename Cfg = DefaultConfig> class LogRing {
public:
  using EntryT = Entry<Cfg>;

  /**
   * @brief The index just before the ring head.
   * @return Index
   */
  [[nodiscard]] Index base() const noexcept { return m_base; }

  /**
   * @brief The term for index Base.
   * @return Term
   */
  [[nodiscard]] Term base_term() const noexcept { return m_base_term; }

  /**
   * @brief The number of items in the log.
   * @return std::uint32_t
   */
  [[nodiscard]] std::uint32_t count() const noexcept { return m_count; }

  /**
   * @brief Is the log full.
   * @return true if it is.
   */
  [[nodiscard]] bool full() const noexcept {
    return m_count >= Cfg::LOG_CAPACITY;
  }

  /**
   * @brief Highest raft index present.
   * @return Index
   */
  [[nodiscard]] Index last_index() const noexcept { return m_base + m_count; }

  /**
   * @brief Pointer to entry at index, or nullptr if the index is at/below the
   * snapshot edge or past the end.
   * @param [in] index The index to find the entry for.
   * @return EntryT*
   */
  [[nodiscard]] EntryT *at(const Index index) noexcept {

    const Index last = last_index();

    if (index <= m_base || index > last) {
      return nullptr;
    }

    const auto slot = static_cast<std::uint32_t>(
        (m_head + (index - m_base - 1)) % Cfg::LOG_CAPACITY);
    return &m_buf[slot];
  }

  /**
   * @brief Pointer to entry at index, or nullptr if the index is at/below the
   * snapshot edge or past the end.
   * @param [in] index The index to find the entry for.
   * @return const EntryT*
   */
  [[nodiscard]] const EntryT *at(const Index index) const noexcept {
    return const_cast<LogRing *>(this)->at(index);
  }

  /**
   * @brief Term at index, nullopt if below the snapshot.
   * @param [in] index The index to find the term for.
   * @return std::optional<Term>
   */
  [[nodiscard]] std::optional<Term> term_at(const Index index) const noexcept {

    if (index == 0) {
      return Term{0};
    }

    if (index == m_base) {
      return m_base_term;
    }

    if (const EntryT *entry = at(index)) {
      return entry->m_term;
    }

    return std::nullopt;
  }

  /**
   * @brief Term of the last entry.
   * @return Term
   */
  [[nodiscard]] Term last_term() const noexcept {

    const Index last = last_index();
    if (last == m_base) {
      return m_base_term;
    }

    const EntryT *entry = at(last);
    return entry ? entry->m_term : m_base_term;
  }

  /**
   * @brief Append one entry to the log.
   * @param [in] entry The entry to append.
   * @return Error Error::LOG_FULL if the ring is full
   */
  [[nodiscard]] Error push(EntryT entry) noexcept {

    if (full()) {
      return Error::LOG_FULL;
    }

    const auto slot =
        static_cast<std::uint32_t>((m_head + m_count) % Cfg::LOG_CAPACITY);
    m_buf[slot] = std::move(entry);
    ++m_count;

    return Error::OK;
  }

  /**
   * @brief Drop every entry at Raft index >= from_index (5.3 conflict
   * handling).
   * @param [in] from_index The index to drop from.
   */
  void truncate_suffix(const Index from_index) noexcept {

    if (from_index <= m_base + 1) {
      m_count = 0;
      return;
    }

    if (from_index > last_index()) {
      return;
    }

    m_count = static_cast<std::uint32_t>(from_index - m_base - 1);
  }

  /**
   * @brief Compact the prefix up to and including index.
   * Index must be live, advance the head past it, keep the live suffix, and
   * adopt (index, term) as the new snapshot edge.
   * @param [in] index
   * @param [in] term
   */
  void compact_to(const Index index, const Term term) noexcept {

    const auto remaining = static_cast<std::uint32_t>(last_index() - index);
    m_head = static_cast<std::uint32_t>((m_head + (index - m_base)) %
                                        Cfg::LOG_CAPACITY);
    m_count = remaining;
    m_base = index;
    m_base_term = term;
  }

  /**
   * @brief Discard the entire live log and reposition the (empty) ring just
   * past a snapshot whose last-included entry is (index, term). Used when an
   * incoming snapshot does NOT match an existing entry (Figure 13 step 7:
   * "Discard the entire log") and during restore.
   * @param [in] index
   * @param [in] term
   */
  void reset_to_snapshot(Index index, Term term) noexcept {
    m_head = 0;
    m_count = 0;
    m_base = index;
    m_base_term = term;
  }

private:
  /** @brief Inline log buffer. */
  std::array<EntryT, Cfg::LOG_CAPACITY> m_buf{};

  /** @brief index of the entry just before the ring head. */
  Index m_base{0};

  /** @brief The term of m_base. */
  Term m_base_term{0};

  /** @brief Number of items in the log. */
  std::uint32_t m_count{0};

  /** @brief The next free index in the log. */
  std::uint32_t m_head{0};
};

} // namespace fog::raft
