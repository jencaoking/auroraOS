#ifndef KERNEL_OTA_HPP
#define KERNEL_OTA_HPP

#include <cstdint>
#include <cstddef>

namespace aurora {

class OtaManager {
public:
    OtaManager();

    // Begin an OTA update, erasing the staging partition
    bool begin_update();

    // Write a chunk of data to the staging partition
    bool write_chunk(uint32_t offset, const uint8_t* data, size_t size);

    // Commit the update, writing the firmware header and marking as UPDATE_PENDING
    bool commit_update(uint32_t image_size, uint32_t expected_crc, uint32_t version);

    // Trigger a system reboot to apply the update
    void reboot();

private:
    uint32_t calculate_crc32(const uint8_t* data, uint32_t length);
};

} // namespace aurora

#endif // KERNEL_OTA_HPP
