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
    VfsManager::instance().lseek(fd, 0);
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

    sys_print("[ElfLoader] Valid ARM ELF detected. Parsing Segments...\r\n");

    // 2. 遍历所有的程序段 (Program Headers)，寻找需要加载的代码和数据
    Elf32_Phdr phdr;
    for (int i = 0; i < ehdr.e_phnum; i++) {
        // 定位到第 i 个程序段头部
        VfsManager::instance().lseek(fd, ehdr.e_phoff + i * ehdr.e_phentsize);
        VfsManager::instance().read(fd, reinterpret_cast<char*>(&phdr), sizeof(phdr));

        // 我们只关心 PT_LOAD 可加载段 (包含了 .text 代码和 .data 变量)
        if (phdr.p_type == PT_LOAD && phdr.p_memsz > 0) {
            // 向我们在第三步写的内核堆 (KernelHeap) 申请一块干净的动态内存
            char* segment_memory = new char[phdr.p_memsz];
            if (!segment_memory) {
                sys_print("[ElfLoader] Out of Memory while allocating segment!\r\n");
                VfsManager::instance().close(fd);
                return false;
            }

            // 从文件中读取二进制指令及初始数据到分配的内存中
            VfsManager::instance().lseek(fd, phdr.p_offset);
            VfsManager::instance().read(fd, segment_memory, phdr.p_filesz);

            // 如果 p_memsz > p_filesz，说明多出来的部分是 .bss 段（未初始化变量），必须清零
            for (uint32_t b = phdr.p_filesz; b < phdr.p_memsz; b++) {
                segment_memory[b] = 0;
            }

            // 计算我们在内存中的真实入口地址：
            uintptr_t raw_entry;
            if (ehdr.e_entry >= phdr.p_vaddr && ehdr.e_entry < phdr.p_vaddr + phdr.p_memsz) {
                raw_entry = reinterpret_cast<uintptr_t>(segment_memory) + (ehdr.e_entry - phdr.p_vaddr);
            } else {
                // 兼容手写 ELF：e_entry 可能是文件偏移而不是虚拟地址
                raw_entry = reinterpret_cast<uintptr_t>(segment_memory) + (ehdr.e_entry - phdr.p_offset);
            }
            
            // 【硬核必杀技】Cortex-M 架构必须运行在 Thumb 状态，地址最低位一定要置 1！
            uintptr_t thumb_entry = raw_entry | 0x01;
            
            // 强转成 C++ 函数指针
            void (*app_entry)(void) = reinterpret_cast<void (*)()>(thumb_entry);

            sys_print("[ElfLoader] Spawning Dynamic Task from RAM...\r\n");

            // 3. 为这个全新程序开辟任务栈，并将其实例化为内核的动态任务！
            // Since this runs in Unprivileged mode, we cannot call Scheduler::instance().create_task() if it touches privileged regions.
            // Wait, Scheduler::create_task() just writes to `tasks` array. It doesn't touch NVIC or ICSR! So it is safe to call from Unprivileged mode!
            uint32_t* app_stack = new uint32_t[512];
            Scheduler::instance().create_task(app_entry, app_stack, 512 * sizeof(uint32_t),
                TaskPriority::Low); // 动态加载的 ELF 应用以低优先级运行
            
            // 为精简工程，加载完第一个核心代码段后直接返回成功
            VfsManager::instance().close(fd);
            return true;
        }
    }

    VfsManager::instance().close(fd);
    return false;
}
