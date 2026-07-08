

#include "common/logging.hpp"
#include "openthread.h"
#include <cstdint>
#include <openthread/network_time.h>
#include <optional>

namespace fog::time_sync {

/**
 * @brief Get synchronised network time in microseconds.
 * @return std::optional<std::uint64_t> std::nullopt if not synced.
 */
inline std::optional<std::uint64_t> get_time_us() {
  otInstance *const ot_inst = openthread_get_default_instance();
  std::uint64_t network_time_us = 0;
  otNetworkTimeStatus status = otNetworkTimeGet(ot_inst, &network_time_us);

  if (status != OT_NETWORK_TIME_SYNCHRONIZED) {
    logging::inf("get_time_us: Time not synced");
    return std::nullopt;
  }

  return network_time_us;
}

} // namespace fog::time_sync
