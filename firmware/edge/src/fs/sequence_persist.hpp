#pragma once

#include <cstdint>
#include <zephyr/drivers/flash.h>
#include <zephyr/fs/nvs.h>
#include <zephyr/storage/flash_map.h>

namespace edge::fs {

/**
 * @brief The persisted CoAP sequence number.
 */
struct PersistedSequence {
  std::uint32_t m_seq;
};

class SequencePersist {

  static constexpr std::uint16_t NVS_ID_SEQ_PERSIST = 1;

public:
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
  PersistedSequence load() {
    PersistedSequence persist{};
    const int ret =
        nvs_read(&m_fs, NVS_ID_SEQ_PERSIST, &persist, sizeof(persist));
    if (ret < 0) {
      // It could just be we didn't have any data loaded in the first place, so
      // an error here is fine.
    }

    return persist;
  }

  /**
   * @brief Persist the raft state to NVS.
   * @param [in] persist The value to persist.
   * @return int result of the write.
   */
  int save(const PersistedSequence persist) {
    return nvs_write(&m_fs, NVS_ID_SEQ_PERSIST, &persist, sizeof(persist));
  }

private:
  /** @brief The filesystem structure.  */
  struct nvs_fs m_fs;
};

} // namespace edge::fs
