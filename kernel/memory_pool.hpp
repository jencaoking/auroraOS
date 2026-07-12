#ifndef MEMORY_POOL_HPP
#define MEMORY_POOL_HPP

#include <stdint.h>
#include <stddef.h>
#include "mutex.hpp"

template <typename T, size_t PoolSize>
class MemoryPool {
private:
    union Node {
        T data;
        Node* next;
    };

    Node buffer_[PoolSize];
    uint32_t allocated_[(PoolSize + 31) / 32];
    Node* free_list_;
    Mutex pool_mutex_;

public:
    MemoryPool() {
        for (size_t i = 0; i < PoolSize - 1; ++i) {
            buffer_[i].next = &buffer_[i + 1];
        }
        buffer_[PoolSize - 1].next = nullptr;
        free_list_ = &buffer_[0];
        for (size_t i = 0; i < (PoolSize + 31) / 32; ++i) {
            allocated_[i] = 0;
        }
    }

    // Allocate an object from the pool in O(1) time
    T* allocate() {
        LockGuard lock(pool_mutex_);
        if (free_list_ == nullptr) {
            return nullptr; // Pool exhausted
        }
        Node* node = free_list_;
        free_list_ = node->next;
        size_t index = node - &buffer_[0];
        allocated_[index / 32] |= (1U << (index % 32));
        return reinterpret_cast<T*>(node);
    }

    // Deallocate an object back to the pool in O(1) time
    void deallocate(T* ptr) {
        if (!ptr) return;
        
        // 边界检查：确保 ptr 在 buffer_ 的范围内
        uintptr_t ptr_addr = reinterpret_cast<uintptr_t>(ptr);
        uintptr_t pool_start = reinterpret_cast<uintptr_t>(&buffer_[0]);
        uintptr_t pool_end = pool_start + sizeof(buffer_);
        
        if (ptr_addr < pool_start || ptr_addr >= pool_end) {
            return; // 越界指针
        }
        
        LockGuard lock(pool_mutex_);
        Node* node = reinterpret_cast<Node*>(ptr);
        size_t index = node - &buffer_[0];
        
        // Double free check
        if (!(allocated_[index / 32] & (1U << (index % 32)))) {
            return; // Already free!
        }
        allocated_[index / 32] &= ~(1U << (index % 32));
        
        node->next = free_list_;
        free_list_ = node;
    }

    size_t get_capacity() const {
        return PoolSize;
    }
};

#endif // MEMORY_POOL_HPP
