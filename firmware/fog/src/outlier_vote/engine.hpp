#pragma once

#include "common/sensor_reading.hpp"
#include "outlier_vote/sensor_batch.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace fog::vote {

using Eui = std::uint64_t;

/**
 * @brief Configuration for the engine parameters.
 */
struct Config {
  std::uint16_t m_tolerance_mm = 200;        // |reading - median| outlier bound
  std::uint16_t m_alert_threshold_mm = 1500; // good reading above this -> alert
  std::uint32_t m_collection_window_ms =
      30000; // Time to wait before snapshotting samples and voting.
  std::uint8_t m_alert_clear_windows = 3; // clean windows needed to clear alert
  std::uint8_t m_reputation_penalty = 20; // lost per outlier
  std::uint8_t m_reputation_recovery = 5; // gained per clean reading
  std::uint8_t m_reputation_min = 20;     // below -> excluded
  std::uint8_t m_reputation_readmit = 40; // at/above -> readmitted
  std::uint8_t m_reputation_initial = 100;
};

/**
 * @brief A helper struct for storing the reputation associated with each Eui.
 */
struct Reputation {
  Eui m_eui = 0;
  std::uint8_t m_reputation = 0;
  bool m_known = false;
  bool m_excluded = false;
};

/**
 * @brief Vote engine provides the logic for accumulating sensor readings and
 * then running a vote across them to identify any outliers for BZT detection.
 */
class VoteEngine {
public:
  using Entry = common::SensorReadingWire;
  using Batch = batch::SensorBatch;
  using Validity = common::IEC61850_Validity;
  using Detail = common::IEC61850_DetailQual;
  static constexpr std::size_t MAX_SENSORS = common::MAX_SENSORS;
  static constexpr std::size_t MAX_BATCH_ENTRIES = common::MAX_SENSORS;
  static constexpr std::uint8_t MIN_REPORTERS = 2;

  static constexpr std::uint8_t MAX_REPUTATION = 100;
  static constexpr std::uint8_t MIN_REPUTATION = 10;

  /**
   * @brief Construct a new Vote Engine object
   * @param [in] cfg The configuration.
   */
  explicit VoteEngine(Config cfg = {}) : m_cfg{cfg} {}

  /**
   * @brief Set a new config.
   * @param cfg the new configuration.
   */
  void set_config(const Config &cfg) noexcept { m_cfg = cfg; }

  /**
   * @brief Get the current configuration
   * @return Config
   */
  [[nodiscard]] Config config() const noexcept { return m_cfg; }

  /**
   * @brief Is an alert active.
   * @return true if there is.
   */
  [[nodiscard]] bool alert_active() const noexcept { return m_alert_active; }

  /**
   * @brief Record the latest reading for a sensor this window. Returns false if
   * the accumulator is full and this EUI has no slot.
   * @param [in] entry the entry to submit.
   * @return true if the entry could be added, false if there are no slots.
   */
  [[nodiscard]] bool accumulate(const Entry &entry) noexcept {

    const int slot = slot_for(entry.m_eui);
    if (slot < 0) {
      return false;
    }

    m_readings[slot] = entry.m_water_level_mm;
    m_validity[slot] = entry.m_validity;
    m_detail[slot] = entry.m_detail;
    m_timestamps[slot] = entry.m_timestamp;
    m_reported |= (1U << slot);
    return true;
  }

  /**
   * @brief Reset the reported set, opening a fresh window. Slot assignments
   * persist so sensors keep stable slot indices across windows.
   */
  void open_window() noexcept { m_reported = 0; }

  /**
   * @brief Close the window and run the vote. Returns the emitted batch, or
   * nullopt if fewer than MIN_REPORTERS reported. The
   * reported set is cleared either way.
   */
  [[nodiscard]] std::optional<Batch> close_window() noexcept {

    std::array<std::uint8_t, MAX_SENSORS> reporters{};
    std::array<std::uint16_t, MAX_SENSORS> vals{};
    std::uint8_t num_reported = 0;
    for (std::uint8_t slot = 0; slot < m_slot_count; ++slot) {
      if ((m_reported & (1U << slot)) != 0) {
        reporters[num_reported] = slot;
        vals[num_reported] = m_readings[slot];
        ++num_reported;
      }
    }

    m_reported = 0;

    // Ensure we have the minimum number of sensors reported.
    if (num_reported < MIN_REPORTERS) {
      return std::nullopt;
    }

    // Get the median over all the sensors.
    const std::uint16_t med = median_n({vals.data(), num_reported});

    Batch out{};
    out.m_count = 0;
    for (std::uint8_t report = 0; report < num_reported; ++report) {
      if (out.m_count >= MAX_BATCH_ENTRIES) {
        break;
      }
      build_entry(out, reporters[report], med);
    }

    if (out.m_count == 0) {
      return std::nullopt;
    }

    return out;
  }

  /**
   * @brief Alert hysteresis over an emitted batch.A good reading at/above the
   * threshold raises alert immediately; it clears after alert_clear_windows
   * consecutive windows with no trigger.
   * @param [in] batch The batch to check for an alert.
   * @return true if an alert is active.
   */
  bool check_alert(const Batch &batch) noexcept {
    bool trigger = false;
    for (std::uint8_t i = 0; i < batch.m_count; ++i) {
      if (batch.m_entries[i].m_validity == Validity::VALIDITY_GOOD &&
          batch.m_entries[i].m_water_level_mm >= m_cfg.m_alert_threshold_mm) {
        trigger = true;
        break;
      }
    }
    if (trigger) {
      m_clean_windows = 0;
      m_alert_active = true;
    } else if (m_alert_active) {
      if (++m_clean_windows >= m_cfg.m_alert_clear_windows) {
        m_alert_active = false;
        m_clean_windows = 0;
      }
    }
    return m_alert_active;
  }

  /**
   * @brief Copy the reputation into the span `out`.
   * @param [out] out user supplied buffer.
   * @return std::size_t the number written.
   */
  std::size_t
  reputation_snapshot(const std::span<Reputation> out) const noexcept {
    std::size_t count = 0;
    for (const auto &reputation : m_reputation) {
      if (reputation.m_known && count < out.size()) {
        out[count++] = reputation;
      }
    }
    return count;
  }

private:
  /**
   * @brief Clamp the reputation between REPUTATION_MIN and REPUTATION_MAX.
   * @param [in] value The value to clamp.
   * @return constexpr std::uint8_t The clamped value.
   */
  [[nodiscard]] static constexpr std::uint8_t
  clamp_reputation(std::int32_t value) noexcept {
    return static_cast<std::uint8_t>(
        std::clamp(value, static_cast<std::int32_t>(MIN_REPUTATION),
                   static_cast<std::int32_t>(MAX_REPUTATION)));
  }

  /**
   * @brief Get the median value for the span of integers in `in`
   * @param [in] in
   * @return std::uint16_t
   */
  [[nodiscard]] static constexpr std::uint16_t
  median_n(std::span<const std::uint16_t> in) noexcept {

    const std::size_t num_sensors =
        std::min(in.size(), static_cast<std::size_t>(MAX_SENSORS));

    // Nothing reported.
    if (num_sensors == 0) {
      return 0;
    }

    std::array<std::uint16_t, MAX_SENSORS> buf;
    std::copy_n(in.data(), num_sensors, buf.begin());

    // std::nth_element only partially sorts up to the exact index we care about
    // the middle.
    auto *const mid = buf.begin() + (num_sensors / 2);
    std::nth_element(buf.begin(), mid, buf.begin() + num_sensors);

    // Handle the even size case like n == 2
    if (num_sensors % 2 == 0) {
      // We need the element immediately preceding the middle point as well.
      auto *const max_it = std::max_element(buf.begin(), mid);
      return static_cast<std::uint16_t>(
          (static_cast<std::uint32_t>(*max_it) + *mid) / 2);
    }

    return *mid;
  }

  /**
   * @brief Get or create a slot for `eui`
   * @param [in] eui The EUI to get a slot for.
   * @return int -1 if no slot found, or the slot number.
   */
  [[nodiscard]] int slot_for(const Eui eui) noexcept {

    // Check if the it already exists.
    for (std::uint8_t i = 0; i < m_slot_count; ++i) {
      if (m_euis[i] == eui) {
        return i;
      }
    }

    // Check we haven't reached the bounds yet.
    if (m_slot_count >= MAX_SENSORS) {
      return -1;
    }

    // Add the new EUI in the slot.
    const std::uint8_t slot = m_slot_count++;
    m_euis[slot] = eui;
    return slot;
  }

  /**
   * @brief Get the reputation struct for teh given eui.
   * @param eui The EUI to get a slot for.
   * @return Reputation* nullptr if the table is full.
   */
  [[nodiscard]] Reputation *rep_for(const Eui eui) noexcept {

    for (auto &reputation : m_reputation) {
      if (reputation.m_known && reputation.m_eui == eui) {
        return &reputation;
      }
    }

    for (auto &reputation : m_reputation) {
      if (!reputation.m_known) {
        reputation = Reputation{eui, m_cfg.m_reputation_initial, true, false};
        return &reputation;
      }
    }

    return nullptr;
  }

  /**
   * @brief Build entry updates an entry in the output batch with whether or not
   * it is an outlier and updates it's reputation.
   * @param [in] out the batch out.
   * @param [in] slot
   * @param [in] med
   */
  void build_entry(Batch &out, const std::uint8_t slot,
                   const std::uint16_t med) noexcept {

    // Get the EUI.
    const Eui eui = m_euis[slot];
    const std::uint16_t val = m_readings[slot];

    // Workout the deviation from the mean.
    const std::uint16_t dev = std::max(val, med) - std::min(val, med);
    const bool is_outlier = dev > m_cfg.m_tolerance_mm;

    // Get the reputation for this eui.
    Reputation *rep = rep_for(eui);

    if (rep != nullptr) {

      // If this reading is an outlier, reduce the reputation and see if it has
      // reach the min reputation, if it has, exclude it.
      if (is_outlier) {
        rep->m_reputation =
            clamp_reputation(rep->m_reputation - m_cfg.m_reputation_penalty);
        if (rep->m_reputation < m_cfg.m_reputation_min) {
          rep->m_excluded = true;
        }
      } else {

        // Is not an outlier, add the recovery regardless and see if it can be
        // readmitted.
        rep->m_reputation =
            clamp_reputation(rep->m_reputation + m_cfg.m_reputation_recovery);
        if (rep->m_excluded &&
            rep->m_reputation >= m_cfg.m_reputation_readmit) {
          rep->m_excluded = false;
        }
      }
    }

    auto &entry = out.m_entries[out.m_count++];
    entry.m_eui = eui;
    entry.m_water_level_mm = val;
    entry.m_seq = 0;
    entry.m_timestamp = m_timestamps[slot];

    const bool excluded = (rep != nullptr) && rep->m_excluded;
    if (excluded) {
      entry.m_validity = Validity::VALIDITY_INVALID;
      entry.m_detail = m_detail[slot] | Detail::DETAIL_OUTLIER;
    } else if (is_outlier) {
      entry.m_validity = Validity::VALIDITY_QUESTIONABLE;
      entry.m_detail = m_detail[slot] | Detail::DETAIL_OUTLIER;
    } else {
      entry.m_validity = m_validity[slot];
      entry.m_detail = m_detail[slot];
    }
  }

  /** @brief The configuration. */
  Config m_cfg{};

  /** @brief Accumulated EUIs. */
  std::array<Eui, MAX_SENSORS> m_euis{};

  /** @brief Accumulated Readings. */
  std::array<std::uint16_t, MAX_SENSORS> m_readings{};

  /** @brief Accumulated Validity. */
  std::array<Validity, MAX_SENSORS> m_validity{};

  /** @brief Accumulated Detail. */
  std::array<Detail, MAX_SENSORS> m_detail{};

  /** @brief Accumulated Timestamps. */
  std::array<std::uint32_t, MAX_SENSORS> m_timestamps{};

  /** @brief Reported bitfield. */
  std::uint32_t m_reported = 0;

  /** @brief Number of slots in use. */
  std::uint8_t m_slot_count = 0;

  /** @brief Reputation array */
  std::array<Reputation, MAX_SENSORS> m_reputation{};

  /** @brief Is an alert active. */
  bool m_alert_active = false;

  /** @brief Number of windows without an alert triggered.  */
  std::uint8_t m_clean_windows = 0;

  static_assert(MAX_SENSORS <= 32, "m_reported bitmask is 32 bits");
};

} // namespace fog::vote
