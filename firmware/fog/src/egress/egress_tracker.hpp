#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace fog::egress {

/**
 * @brief Hold the state of the message being sent over LoRaWAN, is it currently
 * being sent, or has it finished sending.
 */
enum class Phase : std::uint8_t { STARTED, SENT };

/**
 * @brief Payload for the egress record that will be submitted to raft.
 */
struct EgressRecord {
  std::uint64_t batch_log_index{0};
  std::uint32_t seq{0};
  Phase phase{Phase::STARTED};
};

static_assert(sizeof(EgressRecord) == 16, "keep the egress record small");

constexpr std::size_t TX_EGRESS_QUEUE_SIZE = 4;

/**
 * @brief A queue to keep track of in flight messages, we should only ever have
 * one message in flight, but the queue is slightly larger just to be sure.
 * @tparam CAP The capacity of the queue.
 */
template <std::size_t CAP = TX_EGRESS_QUEUE_SIZE> class EgressTracker {
public:
  /**
   * @brief On apply is called by the raft coordinator, it tracks on all raft
   * nodes what messages are in flight or finished.
   * @param [in] rec The newly submitted record.
   */
  void on_apply(const EgressRecord &rec) noexcept {

    if (rec.seq >= m_next_seq) {
      m_next_seq = rec.seq + 1;
    }

    // Update in place if we already track this seq.
    for (std::size_t i = 0; i < CAP; ++i) {
      if (m_valid[i] && m_ring[i].seq == rec.seq) {
        m_ring[i].phase = rec.phase;
        return;
      }
    }

    // Otherwise claim the next slot and over write the oldest message.
    m_ring[m_head] = rec;
    m_valid[m_head] = true;
    m_head = (m_head + 1) % CAP;
  }

  /** @brief Return type for outstanding. */
  struct Outstanding {
    std::uint64_t m_log_index;
    std::uint32_t m_sequence;
  };

  /**
   * @brief Get the highest record that has been started but not send. Returns
   * an index into the log to get it from raft.
   * @return std::optional<Outstanding>
   */
  [[nodiscard]] std::optional<Outstanding> outstanding() const noexcept {
    std::optional<std::uint32_t> best_sequence;
    std::uint64_t best_index = 0;
    for (std::size_t i = 0; i < CAP; ++i) {
      if (m_valid[i] && m_ring[i].phase == Phase::STARTED) {
        if (!best_sequence.has_value() ||
            m_ring[i].seq > best_sequence.value()) {
          best_sequence = m_ring[i].seq;
          best_index = m_ring[i].batch_log_index;
        }
      }
    }

    if (best_sequence.has_value()) {
      return std::nullopt;
    }

    return {best_index, best_sequence.value()};
  }

  /**
   * @brief Return the next monotonic sequence number.
   * @return std::uint32_t
   */
  [[nodiscard]] std::uint32_t next_seq() const noexcept { return m_next_seq; }

private:
  /** @brief The ring of records. */
  std::array<EgressRecord, CAP> m_ring{};

  /** @brief Whether or not the current record is valid. */
  std::array<bool, CAP> m_valid{};

  /** @brief Head of the log */
  std::size_t m_head{0};

  /** @brief The next sequence. */
  std::uint32_t m_next_seq{0};
};

} // namespace fog::egress
