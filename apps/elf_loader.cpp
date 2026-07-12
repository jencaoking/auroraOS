#include "elf_loader.hpp"
#include "elf.hpp"
#include "vfs.hpp"
#include "memory.hpp"
#include "task.hpp"
#include "syscall.hpp"

// We use sys_print since ElfLoader runs in the context of the user task that called it (shell_task).
// The original prompt used safe_print, but we have migrated to sys_print for isolation.
// Let's create a small wrapper if needed, or just use sys_print directly.

bool ElfLoader::load_and_exec(const char* filepath) {
    int fd = VfsManager::instance().open(filepath);
    if (fd < 0) {
        sys_print("[ElfLoader] Error: Cannot open executable file!\r\n");
        return false;
    }

    // 1. 读取并校验 ELF 文件头部
    Elf32_Ehdr ehdr;
    VfsManager::instance().lseek(fd, 0, 0);
    int read_bytes = VfsManager::instance().read(fd, reinterpret_cast<char*>(&ehdr), sizeof(ehdr));
    
    if (read_bytes != sizeof(ehdr) ||
        ehdr.e_ident[0] != ELFMAG0 || ehdr.e_ident[1] != ELFMAG1 ||
        ehdr.e_ident[2] != ELFMAG2 || ehdr.e_ident[3] != ELFMAG3) {
        sys_print("[ElfLoader] Error: Invalid ELF Magic Number!\r\n");
        VfsManager::instance().close(fd);
        return false;
    }

    if (ehdr.e_machine != EM_ARM) {
        sys_print("[ElfLoader] Error: Not an ARM Cortex-M architecture binary!\r\n");
        VfsManager::instance().close(fd);
        return false;
    }

    if (ehdr.e_phentsize != sizeof(Elf32_Phdr)) {
        sys_print("[ElfLoader] Error: Invalid Program Header size!\r\n");
        VfsManager::instance().close(fd);
        return false;
    }

    sys_print("[ElfLoader] Valid ARM ELF detected. Parsing Segments...\r\n");

    // 第一遍遍历：计算所有 PT_LOAD 段在内存中的跨度
    Elf32_Phdr phdr;
    uint32_t min_vaddr = 0xFFFFFFFF;
    uint32_t max_vaddr = 0;
    bool has_loadable_segment = false;

    for (int i = 0; i < ehdr.e_phnum; i++) {
        VfsManager::instance().lseek(fd, ehdr.e_phoff + i * ehdr.e_phentsize, 0);
        VfsManager::instance().read(fd, reinterpret_cast<char*>(&phdr), sizeof(phdr));

        if (phdr.p_type == PT_LOAD && phdr.p_memsz > 0) {
            has_loadable_segment = true;
            if (phdr.p_vaddr < min_vaddr) min_vaddr = phdr.p_vaddr;
            if (phdr.p_vaddr + phdr.p_memsz > max_vaddr) max_vaddr = phdr.p_vaddr + phdr.p_memsz;
            
            // 安全检查：防止畸形 ELF 导致缓冲区溢出
            if (phdr.p_filesz > phdr.p_memsz) {
                sys_print("[ElfLoader] Error: p_filesz > p_memsz!\r\n");
                VfsManager::instance().close(fd);
                return false;
            }
        }
    }

    if (!has_loadable_segment) {
        sys_print("[ElfLoader] Error: No loadable segments found!\r\n");
        VfsManager::instance().close(fd);
        return false;
    }

    // 分配整块连续内存容纳所有段
    uint32_t total_memsz = max_vaddr - min_vaddr;
    char* segment_memory = new char[total_memsz];
    if (!segment_memory) {
        sys_print("[ElfLoader] Out of Memory while allocating segment!\r\n");
        VfsManager::instance().close(fd);
        return false;
    }

    // 第二遍遍历：实际加载数据
    for (int i = 0; i < ehdr.e_phnum; i++) {
        VfsManager::instance().lseek(fd, ehdr.e_phoff + i * ehdr.e_phentsize, 0);
        VfsManager::instance().read(fd, reinterpret_cast<char*>(&phdr), sizeof(phdr));

        if (phdr.p_type == PT_LOAD && phdr.p_memsz > 0) {
            uint32_t offset_in_mem = phdr.p_vaddr - min_vaddr;
            
            VfsManager::instance().lseek(fd, phdr.p_offset, 0);
            VfsManager::instance().read(fd, segment_memory + offset_in_mem, phdr.p_filesz);

            // 清零 BSS 区域
            for (uint32_t b = phdr.p_filesz; b < phdr.p_memsz; b++) {
                segment_memory[offset_in_mem + b] = 0;
            }
        }
    }

    // 计算我们在内存中的真实入口地址
    uintptr_t raw_entry;
    if (ehdr.e_entry >= min_vaddr && ehdr.e_entry < max_vaddr) {
        raw_entry = reinterpret_cast<uintptr_t>(segment_memory) + (ehdr.e_entry - min_vaddr);
    } else {
        sys_print("[ElfLoader] Error: e_entry out of bounds!\r\n");
        delete[] segment_memory;
        VfsManager::instance().close(fd);
        return false;
    }
    
    // 【硬核必杀技】Cortex-M 架构必须运行在 Thumb 状态，地址最低位一定要置 1！
    uintptr_t thumb_entry = raw_entry | 0x01;
    void (*app_entry)(void) = reinterpret_cast<void (*)()>(thumb_entry);

    sys_print("[ElfLoader] Spawning Dynamic Task from RAM...\r\n");

    uint32_t* app_stack = new uint32_t[512];
    if (!Scheduler::instance().create_task(app_entry, app_stack, 512 * sizeof(uint32_t), TaskPriority::Low)) {
        sys_print("[ElfLoader] Error: task table full, cannot spawn loaded program!\r\n");
        delete[] app_stack;
        delete[] segment_memory;
        VfsManager::instance().close(fd);
        return false;
    }

    VfsManager::instance().close(fd);
    return true;
}
