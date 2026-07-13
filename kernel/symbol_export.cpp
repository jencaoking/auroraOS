#include "symbol_export.hpp"
#include "posix.hpp"

// Forward declare sys_print wrapper or we can just use the syscall inline version inside the ELF directly.
// To provide sys_print as a callable symbol, we create a non-inline wrapper.
extern "C" void sys_print_wrapper(const char* str) {
    // Note: since sys_print is inline, calling it here works fine.
    // However, including syscall.hpp might be problematic if not in kernel space or conflicts.
    // Let's just forward it using the SVC instruction manually to ensure it's a real function.
    __asm__ volatile (
        "mov r0, %0\n\t"
        "svc 0x01\n\t"
        : 
        : "r"(str)
        : "r0"
    );
}

const KernelSymbol kernel_symtab[] = {
    {"sys_print", reinterpret_cast<uintptr_t>(&sys_print_wrapper)},
    {"open", reinterpret_cast<uintptr_t>(&open)},
    {"close", reinterpret_cast<uintptr_t>(&close)},
    {"read", reinterpret_cast<uintptr_t>(&read)},
    {"write", reinterpret_cast<uintptr_t>(&write)},
    {"ioctl", reinterpret_cast<uintptr_t>(&ioctl)},
    {"lseek", reinterpret_cast<uintptr_t>(&lseek)},
    {"sleep", reinterpret_cast<uintptr_t>(&sleep)},
    {"usleep", reinterpret_cast<uintptr_t>(&usleep)},
    {"sem_init", reinterpret_cast<uintptr_t>(&sem_init)},
    {"sem_wait", reinterpret_cast<uintptr_t>(&sem_wait)},
    {"sem_post", reinterpret_cast<uintptr_t>(&sem_post)},
    {"sem_destroy", reinterpret_cast<uintptr_t>(&sem_destroy)},
};

const int kernel_symtab_size = sizeof(kernel_symtab) / sizeof(kernel_symtab[0]);
