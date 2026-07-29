#pragma once

#include <cstdint>
#include <fmt/core.h>
#include <fmt/format.h>
#include <optional>
#include <span>
#include <string_view>

namespace edge::config {

/**
 * @brief A helper class for storing the min, max and default values for a
 config item.
 */
struct Field {
  std::uint32_t m_min;
  std::uint32_t m_max;
  std::uint32_t m_def;

  /**
   * @brief Check that a value is in range (between min and max).
   * @param [in] val The value to check.
   * @return true If it is in range.
   */
  [[nodiscard]] constexpr bool
  in_range(const std::uint32_t val) const noexcept {
    return val >= m_min && val <= m_max;
  }

  /**
   * @brief Return either the value val or the default value if it is out of
   * range.
   * @param [in] val The value to check.
   * @return constexpr std::uint32_t
   */
  [[nodiscard]] constexpr std::uint32_t
  or_default(const std::uint32_t val) const noexcept {
    return in_range(val) ? val : m_def;
  }
};

/**
 * @brief Values struct to store the updated values.
 */
struct Values {
  std::uint32_t default_sleep_s;
  std::uint32_t sensor_warmup_ms;
  std::uint32_t sensor_timeout_ms;
  std::uint32_t ground_distance_mm;
};

/**
 * @brief Update struct allowing only the required values to be updated.
 */
struct Update {
  std::optional<std::uint32_t> default_sleep_s;
  std::optional<std::uint32_t> sensor_warmup_ms;
  std::optional<std::uint32_t> sensor_timeout_ms;
  std::optional<std::uint32_t> ground_distance_mm;
  std::optional<std::uint32_t> alert_sleep_s;
};

class ConfigStore {
public:
  /** @brief Format string for serialisation. */
  static constexpr std::string_view SERIALISATION_FORMAT =
      R"({{"default":{},"warmup":{},"timeout":{},"ground":{}}})";

  /** @brief Constexpr default values. */
  static constexpr Field SLEEP_S{1, 3600, 60};
  static constexpr Field WARMUP_MS{1, 100000, 100};
  static constexpr Field SENSOR_TIMEOUT_MS{1, 1000000, 1000};
  static constexpr Field GROUND_DIST_MM{0, 10000, 2000};
  static constexpr Field ALERT_SLEEP_S = SLEEP_S;

  /**
   * @brief Constructor.
   */
  ConfigStore() noexcept { load_defaults(); }

  /**
   * @brief Load the default values from the constant fields.
   */
  void load_defaults() noexcept {
    m_values = Values{.default_sleep_s = SLEEP_S.m_def,
                      .sensor_warmup_ms = WARMUP_MS.m_def,
                      .sensor_timeout_ms = SENSOR_TIMEOUT_MS.m_def,
                      .ground_distance_mm = GROUND_DIST_MM.m_def};

    m_alert_sleep_s = ALERT_SLEEP_S.m_def;
    m_alert_active = false;
  }

  void load_from(const Values &stored) noexcept {
    m_values.default_sleep_s = SLEEP_S.or_default(stored.default_sleep_s);
    m_values.sensor_warmup_ms = WARMUP_MS.or_default(stored.sensor_warmup_ms);
    m_values.sensor_timeout_ms =
        SENSOR_TIMEOUT_MS.or_default(stored.sensor_timeout_ms);
    m_values.ground_distance_mm =
        GROUND_DIST_MM.or_default(stored.ground_distance_mm);
  }

  struct ApplyResult {
    bool persisted_changed = false; // a default field changed -> save
    bool alert_changed = false;     // alert interval set -> reschedule
  };

  /**
   * @brief Apply the readings in update to the store.
   * Each reading is checked first to see if it is within an acceptable range.
   * @param update The updated values.
   * @return ApplyResult Whether the values were updated and need persisting and
   * whether an alert was activated.
   */
  ApplyResult apply(const Update &update) noexcept {
    ApplyResult res{};

    if (update.default_sleep_s && SLEEP_S.in_range(*update.default_sleep_s)) {
      m_values.default_sleep_s = *update.default_sleep_s;
      res.persisted_changed = true;
    }

    if (update.sensor_warmup_ms &&
        WARMUP_MS.in_range(*update.sensor_warmup_ms)) {
      m_values.sensor_warmup_ms = *update.sensor_warmup_ms;
      res.persisted_changed = true;
    }

    if (update.sensor_timeout_ms &&
        SENSOR_TIMEOUT_MS.in_range(*update.sensor_timeout_ms)) {
      m_values.sensor_timeout_ms = *update.sensor_timeout_ms;
      res.persisted_changed = true;
    }

    if (update.ground_distance_mm &&
        GROUND_DIST_MM.in_range(*update.ground_distance_mm)) {
      m_values.ground_distance_mm = *update.ground_distance_mm;
      res.persisted_changed = true;
    }

    // Alert: separate, NOT persisted, does not touch default_sleep_s.
    if (update.alert_sleep_s && ALERT_SLEEP_S.in_range(*update.alert_sleep_s)) {
      m_alert_sleep_s = *update.alert_sleep_s;
      m_alert_active = true;
      res.alert_changed = true;
    }

    return res;
  }

  /**
   * @brief Clear the alert status.
   */
  void clear_alert() noexcept { m_alert_active = false; }

  /**
   * @brief Set an alert from the alert message.
   */
  void set_alert(const std::uint16_t alert_time) noexcept {
    m_alert_active = true;
    m_alert_sleep_s = alert_time;
  }

  /**
   * @brief Serialise the configuration into json.
   * @param [out] out The output buffer to write the data into.
   * @return std::optional<std::string_view> A string view to the data in out,
   * but begin and end are terminated at actual size written.
   */
  [[nodiscard]] std::optional<std::string_view>
  serialize(std::span<char> out) const noexcept {

    const auto res = fmt::format_to_n(
        out.data(), out.size(), SERIALISATION_FORMAT, m_values.default_sleep_s,
        m_values.sensor_warmup_ms, m_values.sensor_timeout_ms,
        m_values.ground_distance_mm);

    if (res.size > out.size()) {
      return std::nullopt;
    }

    return std::string_view{out.data(), res.size};
  }

  /**
   * @brief Get a const& to the config values.
   * @return const Values&
   */
  [[nodiscard]] const Values &values() const noexcept { return m_values; }

  /**
   * @brief Get the sleep time, dependant on whether or not an alert is active.
   * @return std::uint32_t
   */
  [[nodiscard]] std::uint32_t effective_sleep_s() const noexcept {
    return m_alert_active ? m_alert_sleep_s : m_values.default_sleep_s;
  }

  /**
   * @brief Sensor warmup time.
   * @return std::uint32_t
   */
  [[nodiscard]] std::uint32_t sensor_warmup_ms() const noexcept {
    return m_values.sensor_warmup_ms;
  }

  /**
   * @brief The sensor timeout time in ms.
   * @return std::uint32_t
   */
  [[nodiscard]] std::uint32_t sensor_timeout_ms() const noexcept {
    return m_values.sensor_timeout_ms;
  }

  /**
   * @brief The distance to the ground in mm.
   * @return std::uint32_t
   */
  [[nodiscard]] std::uint32_t ground_distance_mm() const noexcept {
    return m_values.ground_distance_mm;
  }

  /**
   * @brief Is an alert currently active.
   * @return true if it is.
   */
  [[nodiscard]] bool alert_active() const noexcept { return m_alert_active; }

private:
  /** @brief The current values stored. */
  Values m_values{};

  /** @brief The alert time which is not persisted. */
  std::uint32_t m_alert_sleep_s{ALERT_SLEEP_S.m_def};

  /** @brief Is an alert active. */
  bool m_alert_active{false};
};

} // namespace edge::config
