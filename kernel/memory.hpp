#ifndef MEMORY_HPP
#define AURORA_MEMORY_HPP

#include <stdint.h>
#include <stddef.h>

class KernelHeap {
private:
    struct BlockHeader {
        size_t size;          // 包含头部在内的总大小
        bool is_free;         // 是否空闲
        BlockHeader* next;    // 指向下一个块的指针
    };

    BlockHeader* head_block;
    size_t total_free_memory;

public:
    static KernelHeap& instance() {
        static KernelHeap heap;
        return heap;
    }

    // 初始化堆，传入链接脚本暴露的起止地址
    void init(void* start_addr, void* end_addr) {
        uintptr_t start = reinterpret_cast<uintptr_t>(start_addr);
        uintptr_t end = reinterpret_cast<uintptr_t>(end_addr);

        // 4字节对齐
        start = (start + 3) & ~3;
        end = end & ~3;

        head_block = reinterpret_cast<BlockHeader*>(start);
        head_block->size = end - start;
        head_block->is_free = true;
        head_block->next = nullptr;

        total_free_memory = head_block->size - sizeof(BlockHeader);
    }

    // 分配内存
    void* allocate(size_t size) {
        // 4字节对齐
        size = (size + 3) & ~3;
        size_t required_space = size + sizeof(BlockHeader);

        BlockHeader* current = head_block;
        while (current != nullptr) {
            if (current->is_free && current->size >= required_space) {
                // 如果剩余空间足够切分，就分裂该块 (Split Block)
                if (current->size >= required_space + sizeof(BlockHeader) + 4) {
                    BlockHeader* next_block = reinterpret_cast<BlockHeader*>(
                        reinterpret_cast<uintptr_t>(current) + required_space
                    );
                    next_block->size = current->size - required_space;
                    next_block->is_free = true;
                    next_block->next = current->next;

                    current->size = required_space;
                    current->next = next_block;
                }

                current->is_free = false;
                // 返回越过 BlockHeader 之后的实际可用内存地址
                return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(current) + sizeof(BlockHeader));
            }
            current = current->next;
        }
        return nullptr; // OOM: 内存不足
    }

    // 释放内存
    void deallocate(void* ptr) {
        if (!ptr) return;

        BlockHeader* target = reinterpret_cast<BlockHeader*>(
            reinterpret_cast<uintptr_t>(ptr) - sizeof(BlockHeader)
        );
        target->is_free = true;

        // 整理内存：自动合并连续的空闲块 (Coalesce Free Blocks)
        BlockHeader* current = head_block;
        while (current != nullptr && current->next != nullptr) {
            if (current->is_free && current->next->is_free) {
                current->size += current->next->size;
                current->next = current->next->next;
            } else {
                current = current->next;
            }
        }
    }
};

#endif
