#define ENABLE_FAULT_INJECTION 1

#include "common/coap_utils.h"
#include "common/logging.hpp"
#include "edge.hpp"
#include "fog/build/fog/tfm/api_ns/interface/include/psa/crypto.h"
#include "openthread.h"
#include <openthread/error.h>
#include <openthread/link.h>
#include <openthread/thread.h>
#include <optional>

namespace {

/** @brief The main application, delay construction. */
std::optional<edge::Edge> App;

auto *get_app() { return App.has_value() ? &App.value() : nullptr; }

/** @brief Flag to handle reattaching to openthread network. */
bool s_initialised = false;

/**
 * @brief OpenThread state change callback.
 * @param flags Flags to check on state change.
 * @param user_data unused.
 */
void on_thread_state_changed(otChangedFlags flags, void *user_data) {

  ARG_UNUSED(user_data);

  otInstance *const inst = openthread_get_default_instance();

  if ((flags & OT_CHANGED_THREAD_ROLE) != 0) {
    const otDeviceRole role = otThreadGetDeviceRole(inst);
    switch (role) {
    case OT_DEVICE_ROLE_CHILD:
    case OT_DEVICE_ROLE_ROUTER:
    case OT_DEVICE_ROLE_LEADER:
      // We have a valid role so start.
      logging::wrn("Device attached to thread network.");
      if (!s_initialised) {
        get_app()->init();
        s_initialised = true;
      }
      break;

    case OT_DEVICE_ROLE_DISABLED:
    case OT_DEVICE_ROLE_DETACHED:
    default:
      logging::wrn("Device detached from thread network.");
      break;
    }
  }
}

constexpr std::uint32_t CSL_PERIOD_US = 30000000;
constexpr std::uint32_t CSL_TIMEOUT_S = 20;

/**
 * @brief CSL needs enabling before openthread is started. Set the CSL window
 * and disable active poling.
 */
int csl_setup() {

  otInstance *const inst = openthread_get_default_instance();

  // Disable polling.
  otError err = otLinkSetPollPeriod(inst, 0);
  if (err != OT_ERROR_NONE) {
    logging::err("Failed to to disable poll period: {}",
                 otThreadErrorToString(err));
    return -1;
  }

  // Enable CSL.
  err = otLinkSetCslPeriod(inst, CSL_PERIOD_US);
  if (err != OT_ERROR_NONE) {
    logging::err("Failed to to set CSL period: {}", otThreadErrorToString(err));
    return -1;
  }

  err = otLinkSetCslTimeout(inst, CSL_TIMEOUT_S);
  if (err != OT_ERROR_NONE) {
    logging::err("Failed to to set CSL timout period: {}",
                 otThreadErrorToString(err));
    return -1;
  }

  return 0;
}

/**
 * @brief State change callback structure, used to register to callback with
 * openthread.
 */
struct openthread_state_changed_callback s_ot_state_chaged_cb = {
    .otCallback = on_thread_state_changed};

} // namespace

int main(void) {

  if (const auto err = coap_init(); err != 0) {
    logging::err("Failed to initialise CoAP");
    return err;
  }

  if (const auto err = csl_setup(); err != 0) {
    logging::err("Failed to initialise CSL");
    return err;
  }

  psa_crypto_init();

  App.emplace();

  openthread_state_changed_callback_register(&s_ot_state_chaged_cb);
  openthread_run();
}
