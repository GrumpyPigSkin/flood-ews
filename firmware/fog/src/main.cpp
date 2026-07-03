#include "application.hpp"
#include "common/coap_utils.h"
#include "common/logging.hpp"
#include <optional>

namespace {

/** @brief Flag to handle reattaching to openthread network. */
bool s_initialised = false;

std::optional<fog::Application> app;

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
      if (!s_initialised) {
        logging::wrn("Device attached started raft.");
        app->init();
        app->start();
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

/**
 * @brief State change callback structure, used to register to callback with
 * openthread.
 */
struct openthread_state_changed_callback s_ot_state_chaged_cb = {
    .otCallback = on_thread_state_changed};

} // namespace

int main(void) {

  const auto ret = coap_init();
  if (ret != 0) {
    return ret;
  }

  auto on_apply = [](auto) { logging::inf("Apply called."); };

  app.emplace();

  openthread_state_changed_callback_register(&s_ot_state_chaged_cb);
  openthread_run();
}
