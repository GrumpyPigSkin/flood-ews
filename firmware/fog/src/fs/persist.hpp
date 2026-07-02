#pragma once

#include "common/logging.hpp"
#include <cstring>
#include <zephyr/drivers/flash.h>
#include <zephyr/fs/nvs.h>
#include <zephyr/storage/flash_map.h>

namespace fog::fs {

/**
 * @brief The structure persisted to memory.
 */
struct RaftPersist {
  uint64_t m_term;
  uint64_t m_voted_for;
};

/**
 * @brief This encapsulates saving raft data to the NVS, all that we need to
 * persist is the last voted for state and term, this protects against double
 * leadership election.
 */
class RaftPersistence {

  static constexpr uint16_t NVS_ID_RAFT_PERSIST = 1;

public:
  /**
   * @brief Initialise the none voltile storage.
   * @return int 0 on success.
   */
  int init() {
    m_fs.flash_device = FIXED_PARTITION_DEVICE(storage_partition);
    m_fs.offset = FIXED_PARTITION_OFFSET(storage_partition);

    struct flash_pages_info info;
    const auto ret =
        flash_get_page_info_by_offs(m_fs.flash_device, m_fs.offset, &info);

    if (ret != 0) {
      return ret;
    }

    m_fs.sector_size = info.size;
    m_fs.sector_count = 4U;

    return nvs_mount(&m_fs);
  }

  /**
   * @brief Call at boot to load the persisted data.
   * @return RaftPersist
   */
  RaftPersist load() {
    RaftPersist persist{.m_term = 0, .m_voted_for = 0};
    const int ret =
        nvs_read(&m_fs, NVS_ID_RAFT_PERSIST, &persist, sizeof(persist));
    if (ret < 0) {
      // It could just be we didn't have any data loaded in the first place, so
      // an error here is fine.
    }
    logging::inf("load loaded state: .m_term={}, .m_voted_for={} ",
                 persist.m_term, persist.m_voted_for);
    return persist;
  }

  /**
   * @brief Persist the raft state to NVS.
   * @param [in] persist The value to persist.
   * @return int result of the write.
   */
  int save(const RaftPersist &persist) {
    return nvs_write(&m_fs, NVS_ID_RAFT_PERSIST, &persist, sizeof(persist));
  }

private:
  /** @brief The filesystem structure.  */
  struct nvs_fs m_fs;
};

} // namespace fog::fs
