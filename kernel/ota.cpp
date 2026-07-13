#include "ota.hpp"
#include "firmware_header.hpp"
#include <cstring>

namespace aurora {

namespace flash_internal {
    constexpr uint32_t FLASH_CTRL_BASE = 0x400FD000;
    volatile uint32_t* const FMA = reinterpret_cast<uint32_t*>(FLASH_CTRL_BASE + 0x000);
    volatile uint32_t* const FMD = reinterpret_cast<uint32_t*>(FLASH_CTRL_BASE + 0x004);
    volatile uint32_t* const FMC = reinterpret_cast<uint32_t*>(FLASH_CTRL_BASE + 0x008);
    constexpr uint32_t FLASH_KEY = 0x71D50000;

    void erase_page(uint32_t address) {
        *FMA = address & ~(1024 - 1);
        *FMC = FLASH_KEY | 0x2; 
        while (*FMC & 0x2) {} 
    }

    void write_word(uint32_t address, uint32_t data) {
        *FMA = address;
        *FMD = data;
        *FMC = FLASH_KEY | 0x1; 
        while (*FMC & 0x1) {} 
    }
}

constexpr uint32_t PART_B_OFFSET = 0x00020000;
constexpr uint32_t PART_SIZE     = 0x00018000; // 96KB
constexpr uint32_t VECTOR_TABLE_OFFSET = 0x00000100; // 256 bytes

OtaManager::OtaManager() {}

bool OtaManager::begin_update() {
    for (uint32_t addr = PART_B_OFFSET; addr < PART_B_OFFSET + PART_SIZE; addr += 1024) {
        flash_internal::erase_page(addr);
    }
    return true;
}

bool OtaManager::write_chunk(uint32_t offset, const uint8_t* data, size_t size) {
    if (offset + size > PART_SIZE - VECTOR_TABLE_OFFSET) {
        return false;
    }

    uint32_t write_addr = PART_B_OFFSET + VECTOR_TABLE_OFFSET + offset;
    const uint32_t* word_data = reinterpret_cast<const uint32_t*>(data);
    size_t words = size / 4;

    for (size_t i = 0; i < words; ++i) {
        flash_internal::write_word(write_addr + (i * 4), word_data[i]);
    }
    return true;
}

uint32_t OtaManager::calculate_crc32(const uint8_t* data, uint32_t length) {
    uint32_t crc = 0xFFFFFFFF;
    for (uint32_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320 & (-(crc & 1)));
        }
    }
    return ~crc;
}

bool OtaManager::commit_update(uint32_t image_size, uint32_t expected_crc, uint32_t version) {
    uint32_t actual_crc = calculate_crc32(
        reinterpret_cast<const uint8_t*>(PART_B_OFFSET + VECTOR_TABLE_OFFSET), 
        image_size
    );

    if (actual_crc != expected_crc) {
        return false;
    }

    FirmwareHeader header;
    header.magic = FIRMWARE_MAGIC;
    header.version = version;
    header.image_size = image_size;
    header.checksum = expected_crc;
    header.status = FirmwareStatus::UPDATE_PENDING;
    header.reserved[0] = 0;
    header.reserved[1] = 0;
    header.reserved[2] = 0;

    const uint32_t* header_words = reinterpret_cast<const uint32_t*>(&header);
    size_t header_word_count = sizeof(FirmwareHeader) / 4;

    for (size_t i = 0; i < header_word_count; ++i) {
        flash_internal::write_word(PART_B_OFFSET + (i * 4), header_words[i]);
    }

    return true;
}

void OtaManager::reboot() {
    volatile uint32_t* AIRCR = reinterpret_cast<uint32_t*>(0xE000ED0C);
    *AIRCR = (0x05FA0000 | 0x04); // SYSRESETREQ
    while (true) {} 
}

} // namespace aurora
