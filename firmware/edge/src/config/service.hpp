#pragma once

#include "common/coap_utils.h"
#include "common/inplace_function.hpp"
#include "common/mutex.hpp"
#include "common/work_task.hpp"
#include "config/store.hpp"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <openthread/coap.h>
#include <openthread/instance.h>
#include <zephyr/data/json.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>

namespace edge::config {

/**
 * @brief Config service for handling CoAP requests and saving settings to flash
 * storage.
 */
class ConfigService {

  /** @brief Storage paths. */
  static constexpr auto *STOAGE_NAME = "sensor";
  static constexpr auto *CFG_PATH = "sensor/cfg";
  static constexpr auto *CFG_KEY = "cfg";
  static constexpr std::size_t JSON_PAYLOAD_SIZE = 5;

public:
  /**
   * @brief Construct a new Config Service object
   * @param [in] on_reschedule Called when an alert comes in and the sensor
   * needs rescheduling.
   * @param [in] uri_path The path over coap to listen for the config.
   */
  explicit ConfigService(stdext::inplace_function<void()> on_reschedule,
                         const char *uri_path)
      : m_reschedule{std::move(on_reschedule)}, m_save_settings([this] {
          std::scoped_lock guard(m_lock);
          save_locked();
        }) {
    m_resource.mUriPath = uri_path;
    m_resource.mHandler = &ConfigService::request_trampoline;
    m_resource.mContext = this;
    m_resource.mNext = nullptr;
  }

  /**
   * @brief Deleted copy and move constructors it is unsafe to copy and move
   * due to the trampoline.
   */
  ConfigService(const ConfigService &) = delete;
  ConfigService(ConfigService &&) = delete;
  ConfigService &operator=(const ConfigService &) = delete;
  ConfigService &operator=(ConfigService &&) = delete;
  ~ConfigService() = default;

  /**
   * @brief Initialise the config service, register the CoAP resource and try
   * load setting from storage.
   */
  void init() {
    settings_subsys_init();
    s_instance = this;
    m_settings.name = STOAGE_NAME;
    m_settings.h_set = &ConfigService::settings_set_trampoline;
    settings_register(&m_settings);
    settings_load();

    otInstance *ot_inst = openthread_get_default_instance();
    otCoapAddResource(ot_inst, &m_resource);
  }

  /**
   * @brief Get the sleep inteval in seconds.
   * @return std::uint32_t
   */
  [[nodiscard]] std::uint32_t sleep_interval_s() const {
    const std::scoped_lock guard(m_lock);
    return m_store.effective_sleep_s();
  }

  /**
   * @brief Get the sensor warmup time in ms.
   * @return std::uint32_t
   */
  [[nodiscard]] std::uint32_t sensor_warmup_ms() const {
    const std::scoped_lock guard(m_lock);
    return m_store.sensor_warmup_ms();
  }

  /**
   * @brief Get the sensor timeout time in ms.
   * @return std::uint32_t
   */
  [[nodiscard]] std::uint32_t sensor_timeout_ms() const {
    const std::scoped_lock guard(m_lock);
    return m_store.sensor_timeout_ms();
  }

  /**
   * @brief Get the ground distance in mm.
   * @return std::uint32_t
   */
  [[nodiscard]] std::uint32_t ground_distance_mm() const {
    const std::scoped_lock guard(m_lock);
    return m_store.ground_distance_mm();
  }

  /**
   * @brief Check if an alert is active.
   */
  void clear_alert() {
    const std::scoped_lock guard(m_lock);
    m_store.clear_alert();
  }

  /**
   * @brief On an alert message, set the alert and reschedule.
   * @param [in] alert_sleep_time
   */
  void set_alert(const std::uint16_t alert_sleep_time) {
    {
      const std::scoped_lock guard(m_lock);
      m_store.set_alert(alert_sleep_time);
    }
    m_reschedule();
  }

private:
  /**
   * @brief Trampoline function for the CoAP request.
   * @param [in] ctx
   * @param [in] msg
   * @param [in] info
   */
  static void request_trampoline(void *ctx, otMessage *msg,
                                 const otMessageInfo *info) {
    static_cast<ConfigService *>(ctx)->on_request(msg, info);
  }

  /**
   * @brief Handle a request to either set or get the configuration.
   * @param [in] msg
   * @param [in] info
   */
  void on_request(otMessage *msg, const otMessageInfo *info) {
    switch (otCoapMessageGetCode(msg)) {
    case OT_COAP_CODE_GET:
      handle_get(msg, info);
      break;
    case OT_COAP_CODE_POST:
      handle_post(msg, info);
      break;
    default:
      break;
    }
  }

  void handle_get(otMessage *msg, const otMessageInfo *info) {
    constexpr std::size_t BUF_SIZE = 128;
    std::array<char, BUF_SIZE> buf;
    std::optional<std::string_view> body;
    {
      const std::scoped_lock guard(m_lock);
      body = m_store.serialize(buf);
    }

    // serialisation failed.
    if (!body) {
      return;
    }

    std::scoped_lock guard{m_ot_lock};
    coap_resp_send(msg, info,
                   reinterpret_cast<std::uint8_t const *>(body->data()),
                   static_cast<int>(body->size()));
  }

  void handle_post(otMessage *msg, const otMessageInfo *info) {
    constexpr std::size_t BUF_SIZE = 64;
    std::array<char, BUF_SIZE> buf;
    int len = buf.size();
    const auto ret = coap_get_data(msg, buf.data(), &len);
    if (ret != 0) {
      return;
    }

    // Add a null to the end to as json_obj_parse required a null terminated
    // string.
    const std::size_t body_size = len;
    char json[BUF_SIZE + 1];
    std::memcpy(json, buf.data(), body_size);
    json[body_size] = '\0';

    ConfigPostPayload payload{};
    const int present = static_cast<int>(
        json_obj_parse(json, body_size, descr(), descr_count(), &payload));

    bool do_reschedule = false;
    if (present >= 0) {
      const Update update = to_update(payload, present);
      ConfigStore::ApplyResult res{};
      {
        const std::scoped_lock guard(m_lock);
        res = m_store.apply(update);
        if (res.persisted_changed) {
          // Submit saving the settings on the system workqueue.
          m_save_settings.submit();
        }
      }
      do_reschedule = res.alert_changed;
    }

    // reschedule if we need to outside the lock.
    if (do_reschedule && m_reschedule) {
      m_reschedule();
    }

    std::scoped_lock guard{m_ot_lock};
    coap_resp_send(msg, info, nullptr, 0);
  }

  /**
   * @brief JSON payload.
   */
  struct ConfigPostPayload {
    std::uint32_t default_sleep_s;
    std::uint32_t sensor_warmup_ms;
    std::uint32_t sensor_timeout_ms;
    std::uint32_t ground_distance_mm;
    std::uint32_t alert_sleep_s;
  };

  /** @brief Bit fields for JSON parsing. */
  enum : int {
    FIELD_DEFAULT = BIT(0),
    FIELD_WARMUP = BIT(1),
    FIELD_TIMEOUT = BIT(2),
    FIELD_GROUND = BIT(3),
    FIELD_ALERT = BIT(4),
  };

  /**
   * @brief JSON parser descriptor.
   * @return const json_obj_descr*
   */
  static const json_obj_descr *descr() {
    static constexpr json_obj_descr desc[JSON_PAYLOAD_SIZE] = {
        JSON_OBJ_DESCR_PRIM(ConfigPostPayload, default_sleep_s,
                            JSON_TOK_NUMBER),
        JSON_OBJ_DESCR_PRIM(ConfigPostPayload, sensor_warmup_ms,
                            JSON_TOK_NUMBER),
        JSON_OBJ_DESCR_PRIM(ConfigPostPayload, sensor_timeout_ms,
                            JSON_TOK_NUMBER),
        JSON_OBJ_DESCR_PRIM(ConfigPostPayload, ground_distance_mm,
                            JSON_TOK_NUMBER),
        JSON_OBJ_DESCR_PRIM(ConfigPostPayload, alert_sleep_s, JSON_TOK_NUMBER),
    };
    return desc;
  }

  /** @brief The number of values we expect. */
  static std::size_t descr_count() { return JSON_PAYLOAD_SIZE; }

  /**
   * @brief Use the JSON data to check if a value is preset in payload and then
   * assign it before we update.
   * @param [in] payload The payload with the data in.
   * @param [in] present A bit field indicating what bits are present.
   * @return Update
   */
  static Update to_update(const ConfigPostPayload &payload,
                          const std::uint32_t present) {
    Update update;
    if ((present & FIELD_DEFAULT) != 0) {
      update.default_sleep_s = payload.default_sleep_s;
    }

    if ((present & FIELD_WARMUP) != 0) {
      update.sensor_warmup_ms = payload.sensor_warmup_ms;
    }

    if ((present & FIELD_TIMEOUT) != 0) {
      update.sensor_timeout_ms = payload.sensor_timeout_ms;
    }

    if ((present & FIELD_GROUND) != 0) {
      update.ground_distance_mm = payload.ground_distance_mm;
    }

    if ((present & FIELD_ALERT) != 0) {
      update.alert_sleep_s = payload.alert_sleep_s;
    }

    return update;
  }

  /**
   * @brief Save configuration to flash, must be called under a lock.
   */
  void save_locked() {
    const Values values = m_store.values();
    const int ret = settings_save_one(CFG_PATH, &values, sizeof(values));
    if (ret != 0) {
      // Log here maybe?
    }
  }

  /**
   * @brief Value handler trampoline for configuration.
   * @param [in] key The name with skipped part that was used as name in handler
   * registration
   * @param [in] len the Size of the data found in the backend.
   * @param [in] read_cb Function provided to read the data from the backend.
   * @param [in] cb_arg Arguments for the read function provided by the backend.
   * @return int
   */
  static int settings_set_trampoline(const char *key, std::size_t len,
                                     settings_read_cb read_cb, void *cb_arg) {
    return (s_instance != nullptr)
               ? s_instance->settings_set(key, len, read_cb, cb_arg)
               : -ENOENT;
  }

  /**
   * @brief Value handler called from the trampoline function.
   */
  int settings_set(const char *name, std::size_t /*len*/,
                   settings_read_cb read_cb, void *cb_arg) {
    const char *next = nullptr;
    if ((settings_name_steq(name, CFG_KEY, &next) != 0) && (next == nullptr)) {
      Values stored{};
      if (read_cb(cb_arg, &stored, sizeof(stored)) >= 0) {
        const std::scoped_lock guard(m_lock);
        m_store.load_from(stored);
        return 0;
      }
    }
    return -ENOENT;
  }

  /** @brief Underlying storage for the configuration. */
  ConfigStore m_store;

  /** @brief Mutex for concurrent access. */
  mutable common::mutex m_lock;

  /** @brief Openthread mutex for guarding openthread function. */
  mutable common::openthread_mutex m_ot_lock;

  /** @brief On reschedule callback. */
  stdext::inplace_function<void()> m_reschedule;

  /** @brief CoAP resource. */
  otCoapResource m_resource{};

  /** @brief Zephyr settings handler. */
  settings_handler m_settings{};

  /** @brief A task to save the settings as this could take a while we don't
   * want to do this on the openthread workqueue. */
  common::WorkTask m_save_settings;

  /** @brief Pointer to self. */
  static inline ConfigService *s_instance{nullptr};
};

} // namespace edge::config
