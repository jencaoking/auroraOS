#include "kernel/firmware_header.hpp"

extern "C" {
    extern uint32_t _sidata;
    extern uint32_t _sdata;
    extern uint32_t _edata;
    extern uint32_t _sbss;
    extern uint32_t _ebss;
}

namespace flash {

constexpr uint32_t FLASH_CTRL_BASE = 0x400FD000;
volatile uint32_t* const FMA = reinterpret_cast<uint32_t*>(FLASH_CTRL_BASE + 0x000);
volatile uint32_t* const FMD = reinterpret_cast<uint32_t*>(FLASH_CTRL_BASE + 0x004);
volatile uint32_t* const FMC = reinterpret_cast<uint32_t*>(FLASH_CTRL_BASE + 0x008);

constexpr uint32_t FLASH_KEY = 0x71D50000;

void erase_page(uint32_t address) {
    *FMA = address & ~(1024 - 1);
    *FMC = FLASH_KEY | 0x2; // Erase
    while (*FMC & 0x2) {} 
}

void write_word(uint32_t address, uint32_t data) {
    *FMA = address;
    *FMD = data;
    *FMC = FLASH_KEY | 0x1; // Write
    while (*FMC & 0x1) {} 
}

} // namespace flash

namespace {

constexpr uint32_t PART_A_OFFSET    = 0x00008000;
constexpr uint32_t PART_B_OFFSET    = 0x00020000;
constexpr uint32_t PART_SIZE        = 0x00018000; // 96KB
constexpr uint32_t VECTOR_TABLE_OFFSET = 0x00000100; // 256 bytes

uint32_t calculate_crc32(const uint8_t* data, uint32_t length) {
    uint32_t crc = 0xFFFFFFFF;
    for (uint32_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320 & (-(crc & 1)));
        }
    }
    return ~crc;
}

} // namespace

extern "C" void kernel_main() {
    using namespace aurora;
    
    FirmwareHeader* part_a = reinterpret_cast<FirmwareHeader*>(PART_A_OFFSET);
    FirmwareHeader* part_b = reinterpret_cast<FirmwareHeader*>(PART_B_OFFSET);

    // 1. Check if PART_B has an update
    if (part_b->magic == FIRMWARE_MAGIC && part_b->status == FirmwareStatus::UPDATE_PENDING) {
        uint32_t crc = calculate_crc32(
            reinterpret_cast<const uint8_t*>(PART_B_OFFSET + VECTOR_TABLE_OFFSET), 
            part_b->image_size
        );
        
        if (crc == part_b->checksum) {
            // OTA Swap (B -> A)
            for (uint32_t addr = PART_A_OFFSET; addr < PART_A_OFFSET + PART_SIZE; addr += 1024) {
                flash::erase_page(addr);
            }
            
            uint32_t bytes_to_copy = VECTOR_TABLE_OFFSET + part_b->image_size;
            uint32_t total_words = (bytes_to_copy + 3) / 4;
            
            uint32_t* src = reinterpret_cast<uint32_t*>(PART_B_OFFSET);
            uint32_t* dst = reinterpret_cast<uint32_t*>(PART_A_OFFSET);
            for (uint32_t i = 0; i < total_words; ++i) {
                flash::write_word(reinterpret_cast<uint32_t>(&dst[i]), src[i]);
            }
            
            // Invalidate PART_B
            flash::erase_page(PART_B_OFFSET);
        } else {
            // Invalid OTA image, erase PART_B header
            flash::erase_page(PART_B_OFFSET);
        }
    }

    // 2. Secure Boot Verification of PART_A
    if (part_a->magic == FIRMWARE_MAGIC) {
        uint32_t crc = calculate_crc32(
            reinterpret_cast<const uint8_t*>(PART_A_OFFSET + VECTOR_TABLE_OFFSET), 
            part_a->image_size
        );
        
        if (crc == part_a->checksum && (part_a->status == FirmwareStatus::VALID || part_a->status == FirmwareStatus::UPDATE_PENDING)) {
            // Boot PART_A
            uint32_t vector_table_addr = PART_A_OFFSET + VECTOR_TABLE_OFFSET;
            
            volatile uint32_t* SCB_VTOR = reinterpret_cast<uint32_t*>(0xE000ED08);
            *SCB_VTOR = vector_table_addr;

            uint32_t app_sp = *reinterpret_cast<uint32_t*>(vector_table_addr);
            uint32_t app_pc = *reinterpret_cast<uint32_t*>(vector_table_addr + 4);

            asm volatile(
                "msr msp, %0\n"
                "bx %1\n"
                :: "r"(app_sp), "r"(app_pc)
            );
        }
    }
    
    // Fallback: Loop forever
    while (true) {}
}
