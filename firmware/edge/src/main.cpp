#include "common/coap_utils.hpp"
#include "common/logging.hpp"
#include "edge.hpp"
#include "openthread.h"
#include <openthread/link.h>
#include <openthread/thread.h>
#include <optional>

namespace {

/** @brief The main application, delay construction. */
std::optional<edge::Edge> App;

auto *get_app() { return App.has_value() ? &*App : nullptr; }

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

/**
 * @brief State change callback structure, used to register to callback with
 * openthread.
 */
struct openthread_state_changed_callback s_ot_state_chaged_cb = {
    .otCallback = on_thread_state_changed};

} // namespace

int main(void) {

  if (const auto err = coap_utils::coap_init(); err != 0) {

    logging::err("Failed to initialise CoAP");
    return -1;
  }

  App.emplace();

  openthread_state_changed_callback_register(&s_ot_state_chaged_cb);
  openthread_run();
}
