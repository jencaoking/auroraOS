#ifndef KERNEL_FIRMWARE_HEADER_HPP
#define KERNEL_FIRMWARE_HEADER_HPP

#include <cstdint>

namespace aurora {

constexpr uint32_t FIRMWARE_MAGIC = 0x41555241; // 'AURA'

enum class FirmwareStatus : uint32_t {
    EMPTY          = 0xFFFFFFFF,
    UPDATE_PENDING = 0x55AA55AA,
    VALID          = 0x12345678,
    CORRUPT        = 0xDEADBEEF
};

struct FirmwareHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t image_size;
    uint32_t checksum; // CRC32
    FirmwareStatus status;
    uint32_t reserved[3]; // Pad to 32 bytes
};

} // namespace aurora

#endif // KERNEL_FIRMWARE_HEADER_HPP
