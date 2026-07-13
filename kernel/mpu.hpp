#ifndef AURORA_MPU_HPP
#define AURORA_MPU_HPP

#include <stdint.h>

class MPU {
private:
    // Cortex-M4 MPU 核心控制寄存器物理地址映射
    static constexpr uintptr_t MPU_TYPE = 0xE000ED90;
    static constexpr uintptr_t MPU_CTRL = 0xE000ED94;
    static constexpr uintptr_t MPU_RNR  = 0xE000ED98; // 区域选择寄存器
    static constexpr uintptr_t MPU_RBAR = 0xE000ED9C; // 区域基地址寄存器
    static constexpr uintptr_t MPU_RASR = 0xE000EDA0; // 区域属性和大小寄存器

    inline static volatile uint32_t* const reg_ctrl = reinterpret_cast<volatile uint32_t*>(MPU_CTRL);
    inline static volatile uint32_t* const reg_rnr  = reinterpret_cast<volatile uint32_t*>(MPU_RNR);
    inline static volatile uint32_t* const reg_rbar = reinterpret_cast<volatile uint32_t*>(MPU_RBAR);
    inline static volatile uint32_t* const reg_rasr = reinterpret_cast<volatile uint32_t*>(MPU_RASR);

public:
    // MPU 权限配置宏
    static constexpr uint32_t AP_NO_ACCESS  = 0b000;
    static constexpr uint32_t AP_PRIV_RW    = 0b001; // 仅内核特权态可读写
    static constexpr uint32_t AP_ALL_RW     = 0b011; // 内核与用户态均可读写
    static constexpr uint32_t AP_PRIV_RO    = 0b101; // 仅内核特权态只读
    static constexpr uint32_t AP_ALL_RO     = 0b110; // 内核与用户态均只读

    static MPU& instance() {
        static MPU mpu;
        return mpu;
    }

    // 禁用 MPU 进行重新配置
    void disable() {
        __asm__ volatile ("dmb\n\t" : : : "memory");
        *reg_ctrl = 0;
        __asm__ volatile ("dsb\n\t" "isb\n\t" : : : "memory");
    }

    // 启用 MPU，并开启硬件默认背景区（保证在没有配到的区域里，特权态依然可以正常操作硬件）
    void enable() {
        // Bit 0: 开启 MPU | Bit 2: PRIVDEFENA (特权背景区启用)
        *reg_ctrl = (1 << 2) | (1 << 0);
        
        // 启用 MemManage 异常 (SHCSR 寄存器的 MEMFAULTENA 位 - Bit 16)
        volatile uint32_t* shcsr = reinterpret_cast<volatile uint32_t*>(0xE000ED24);
        *shcsr |= (1 << 16);

        __asm__ volatile ("dsb\n\t" "isb\n\t" : : : "memory");
    }

    // 配置单个保护区域
    // region_num: 0~7 | base_addr: 需与 size 对齐 | size_power_of_2: 例如 256字节为 8 (2^8=256)
    void configure_region(uint8_t region_num, uintptr_t base_addr, uint8_t size_power_of_2, uint32_t ap, bool execute_never = false, bool is_device = false) {
        // Ensure base_addr is aligned to size
        uint32_t alignment_mask = (1 << size_power_of_2) - 1;
        if (base_addr & alignment_mask) {
            // Not aligned, behavior undefined or error. We'll force align for safety here or assert
            base_addr &= ~alignment_mask;
        }
        
        *reg_rnr = region_num;
        *reg_rbar = base_addr & ~0x1F; // 清空低5位配置位，只留下基地址

        uint32_t rasr_val = (1 << 0); // Bit 0: ENABLE
        rasr_val |= ((size_power_of_2 - 1) & 0x1F) << 1; // 设定内存区域大小
        rasr_val |= (ap & 0x7) << 24; // 设定访问权限
        
        if (is_device) {
            // Device memory attributes (B=1, C=0, TEX=0)
            rasr_val |= (1 << 16); 
        } else {
            // Normal memory attributes (Write-Back, Read/Write allocate)
            rasr_val |= (1 << 17) | (1 << 16); 
        }

        if (execute_never) {
            rasr_val |= (1 << 28); // Bit 28: XN (Execute Never) 严禁执行命令，防止缓冲区溢出攻击！
        }

        *reg_rasr = rasr_val;
    }

    // 动态更新用户任务的专属沙盒区域（在进程上下文切换 PendSV 时被迅速调用）
    void update_user_sandbox(uintptr_t stack_base, uint8_t size_power_of_2) {
        // 使用 Region 7 作为当前运行用户的动态栈沙盒以避免与可能的 Region 2 冲突
        configure_region(7, stack_base, size_power_of_2, AP_ALL_RW, true, false);
    }
};

#endif
