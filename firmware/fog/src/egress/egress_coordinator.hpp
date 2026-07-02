#pragma once

#include "common/mutex.hpp"
#include "egress_tracker.hpp"
#include <cstring>
#include <functional>
#include <mutex>

namespace fog::egress {

/**
 * @brief Hooks into the rest of the application to inject callbacks.
 * @tparam BatchT
 */
template <typename EntryT, typename BatchT> struct Hooks {

  /** @brief Is this node the leader right now. */
  std::function<bool()> m_is_leader;

  /**
   * @brief Commit an EGRESS record to the Raft log. Return true if accepted.
   */
  std::function<bool(const EgressRecord &rec)> m_submit_egress;

  /** @brief Read the SensorBatch back out of the replicated log by index. */
  std::function<EntryT const *(std::uint64_t index)> m_read_entry;

  /** @brief Hand the encoded bytes to the E5 module. */
  std::function<void(const BatchT &batch, std::uint64_t term,
                     std::uint64_t index, std::uint32_t seq)>
      m_lorawan_send;

  /** @brief Check all hooks are present. */
  [[nodiscard]] bool valid() const noexcept {
    return m_is_leader && m_submit_egress && m_read_entry && m_lorawan_send;
  }
};

/**
 * @brief The main LoRaWAN coordinator. Handles when messages are suspected to
 * be partially sent. It collects the message from the log, resubmits the
 * payload with the same sequence id, then on the gateway side the application
 * can deduplicate.
 */
template <typename EntryT, typename BatchT, std::size_t MAX_PAYLOAD = 242,
          std::size_t CAP = TX_EGRESS_QUEUE_SIZE>
class EgressCoordinator {
public:
  /**
   * @brief Constructor
   * @param [in] hooks The hooks into the rest of the application.
   */
  explicit EgressCoordinator(Hooks<EntryT, BatchT> hooks) noexcept
      : m_hooks{std::move(hooks)} {}

  [[nodiscard]] bool valid() const noexcept { return m_hooks.valid(); }

  /**
   * @brief Called on all append types so all targets track outstanding
   * messages.
   * @param [in] rec the record to track.
   */
  void on_egress_applied(const EgressRecord &rec) noexcept {
    std::scoped_lock guard(m_mutex);
    m_tracker.on_apply(rec);
  }

  /**
   * @brief LEADER ONLY. Begin egress of the batch already committed at
   * `batch_index`.
   * @param batch_index
   */
  void begin_egress(const std::uint64_t batch_index) noexcept {
    std::scoped_lock guard(m_mutex);
    if (!m_hooks.m_is_leader()) {
      return;
    }

    const std::uint32_t seq = m_tracker.next_seq();

    // Commit STARTED first.
    const EgressRecord started{batch_index, seq, Phase::STARTED};
    if (!m_hooks.m_submit_egress(started)) {
      return;
    }

    // Rebuild payload from the batch in the log and send.
    dispatch(seq, batch_index);
  }

  /**
   * @brief Called when a message has been successfully transmitted using the E5
   * mini.
   * @param [in] seq The sequence ID of that batch.
   * @param [in] batch_index The batch index.
   */
  void on_lorawan_ok(const std::uint32_t seq,
                     const std::uint64_t batch_index) noexcept {
    // Commit SENT.
    std::scoped_lock guard(m_mutex);
    const EgressRecord sent{batch_index, seq, Phase::SENT};
    (void)m_hooks.m_submit_egress(sent);
  }

  /**
   * @brief Called when this node becomes a leader. Resends any messages prior
   * leader started but never confirmed.
   */
  void on_became_leader() noexcept {
    std::scoped_lock guard(m_mutex);
    const auto match = m_tracker.outstanding();
    if (!match) {
      return; // nothing was left in flight
    }
    // Re-drive the send for the outstanding seq. No new STARTED record since we
    // are completing an existing seq, not opening a new one.
    dispatch(match.value().m_sequence, match.value().m_log_index);
  }

private:
  /**
   * @brief Read a batch from the log, send it to the e5-mini with the sequence
   * ID.
   * @param [in] seq This messages sequence.
   * @param [in] batch_index This batches ID.
   */
  void dispatch(const std::uint32_t seq,
                const std::uint64_t batch_index) noexcept {
    const auto *entry = m_hooks.m_read_entry(batch_index);

    if (entry == nullptr) {
      // Message is lost from the log, there isn't much more we can do to get it
      // back.
      on_lorawan_ok(seq, batch_index);
      return;
    }

    BatchT batch;
    if (entry->m_data_len < sizeof(batch)) {
      return;
    }
    std::memcpy(&batch, &entry->m_data, sizeof(batch));

    m_hooks.m_lorawan_send(batch, entry->m_term, entry->m_index, seq);
  }

  /** @brief The callback hooks. */
  Hooks<EntryT, BatchT> m_hooks{};

  /** @brief The tracker for messages. */
  EgressTracker<CAP> m_tracker{};

  /** @brief mutex to protect internal state. */
  mutable common::mutex m_mutex;
};

} // namespace fog::egress
