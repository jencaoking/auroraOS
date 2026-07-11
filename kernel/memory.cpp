#include "memory.hpp"
#include <stddef.h>

// 声明链接脚本里暴露的外部边界符号
extern "C" {
    extern uint32_t _heap_start;
    extern uint32_t _heap_end;
}

// 覆写标准 C++ 全局 new 运算符
void* operator new(size_t size) {
    return KernelHeap::instance().allocate(size);
}

void* operator new[](size_t size) {
    return KernelHeap::instance().allocate(size);
}

// 覆写标准 C++ 全局 delete 运算符
void operator delete(void* ptr) noexcept {
    KernelHeap::instance().deallocate(ptr);
}

void operator delete[](void* ptr) noexcept {
    KernelHeap::instance().deallocate(ptr);
}

// 针对 C++ 虚函数或特殊销毁机制的防报错桩函数
void operator delete(void* ptr, size_t size) noexcept {
    (void)size;
    KernelHeap::instance().deallocate(ptr);
}

void operator delete[](void* ptr, size_t size) noexcept {
    (void)size;
    KernelHeap::instance().deallocate(ptr);
}
