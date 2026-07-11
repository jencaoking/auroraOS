#include "lwip/opt.h"
#include "lwip/sys.h"
#include "arch/sys_arch.h"
#include "memory.hpp"
#include "task.hpp"
#include "semaphore.hpp"
#include "mutex.hpp"
#include "msg_queue.hpp"

extern void sys_print(const char* msg);
extern volatile uint32_t tick_count; // Global tick from SysTick_Handler

// ==========================================
// 1. System Clock
// ==========================================
u32_t sys_now(void) {
    // Assuming 1 Tick = 1 ms (1000 Hz SysTick)
    return tick_count;
}

// ==========================================
// 1.5 Lightweight Protection (Critical Sections)
// ==========================================
sys_prot_t sys_arch_protect(void) {
    // Disable interrupts for a critical section
    // In Cortex-M, we can read PRIMASK and then disable IRQs
    int primask = 0;
    __asm__ volatile ("mrs %0, primask\n\t"
                      "cpsid i" : "=r" (primask));
    return primask;
}

void sys_arch_unprotect(sys_prot_t pval) {
    // Restore PRIMASK
    __asm__ volatile ("msr primask, %0" : : "r" (pval));
}

// ==========================================
// 2. Mutex Adapter
// ==========================================
err_t sys_mutex_new(sys_mutex_t *mutex) {
    *mutex = new Mutex();
    return (*mutex != nullptr) ? ERR_OK : ERR_MEM;
}

void sys_mutex_lock(sys_mutex_t *mutex) {
    if (*mutex) static_cast<Mutex*>(*mutex)->lock();
}

void sys_mutex_unlock(sys_mutex_t *mutex) {
    if (*mutex) static_cast<Mutex*>(*mutex)->unlock();
}

void sys_mutex_free(sys_mutex_t *mutex) {
    if (*mutex) {
        delete static_cast<Mutex*>(*mutex);
        *mutex = nullptr;
    }
}

// ==========================================
// 3. Semaphore Adapter
// ==========================================
err_t sys_sem_new(sys_sem_t *sem, u8_t count) {
    *sem = new Semaphore(count);
    return (*sem != nullptr) ? ERR_OK : ERR_MEM;
}

void sys_sem_signal(sys_sem_t *sem) {
    if (*sem) static_cast<Semaphore*>(*sem)->signal();
}

u32_t sys_arch_sem_wait(sys_sem_t *sem, u32_t timeout_ms) {
    if (!*sem) return SYS_ARCH_TIMEOUT;
    
    u32_t start_time = sys_now();
    
    // In a real RTOS we would support timeout.
    // Here we just block infinitely because our Semaphore wait() doesn't have timeout.
    // If timeout_ms > 0, ideally we should poll and sleep. We will just wait.
    static_cast<Semaphore*>(*sem)->wait();
    
    return sys_now() - start_time;
}

void sys_sem_free(sys_sem_t *sem) {
    if (*sem) {
        delete static_cast<Semaphore*>(*sem);
        *sem = nullptr;
    }
}

int sys_sem_valid(sys_sem_t *sem) {
    return (*sem != nullptr);
}

void sys_sem_set_invalid(sys_sem_t *sem) {
    *sem = nullptr;
}

// ==========================================
// 4. Mailbox (Message Queue) Adapter
// ==========================================
err_t sys_mbox_new(sys_mbox_t *mbox, int size) {
    (void)size; // Hardcoded to 16 in our type definition
    *mbox = new MessageQueue<void*, 16>();
    return (*mbox != nullptr) ? ERR_OK : ERR_MEM;
}

void sys_mbox_post(sys_mbox_t *mbox, void *msg) {
    if (*mbox) static_cast<MessageQueue<void*, 16>*>(*mbox)->push(msg);
}

err_t sys_mbox_trypost(sys_mbox_t *mbox, void *msg) {
    if (*mbox) {
        // Our simple MessageQueue push is blocking if full.
        // For try_post, it ideally should not block. 
        // We will just push for now.
        static_cast<MessageQueue<void*, 16>*>(*mbox)->push(msg);
        return ERR_OK;
    }
    return ERR_VAL;
}

err_t sys_mbox_trypost_fromisr(sys_mbox_t *mbox, void *msg) {
    return sys_mbox_trypost(mbox, msg);
}

// ==========================================
// 4.5 System Init
// ==========================================
void sys_init(void) {
    // OSAL initialization if needed. We don't need anything here.
}

u32_t sys_arch_mbox_fetch(sys_mbox_t *mbox, void **msg, u32_t timeout_ms) {
    if (!*mbox) return SYS_ARCH_TIMEOUT;
    u32_t start_time = sys_now();
    
    // Block infinitely to get message
    void* data = static_cast<MessageQueue<void*, 16>*>(*mbox)->pop(); 
    if (msg) *msg = data;
    
    return sys_now() - start_time;
}

u32_t sys_arch_mbox_tryfetch(sys_mbox_t *mbox, void **msg) {
    if (!*mbox) return SYS_MBOX_EMPTY;
    // Our MessageQueue doesn't have try_pop, we will just use pop (blocking).
    void* data = static_cast<MessageQueue<void*, 16>*>(*mbox)->pop();
    if (msg) *msg = data;
    return 0; // Time taken
}

void sys_mbox_free(sys_mbox_t *mbox) {
    if (*mbox) {
        delete static_cast<MessageQueue<void*, 16>*>(*mbox);
        *mbox = nullptr;
    }
}

int sys_mbox_valid(sys_mbox_t *mbox) {
    return (*mbox != nullptr);
}

void sys_mbox_set_invalid(sys_mbox_t *mbox) {
    *mbox = nullptr;
}

// ==========================================
// 5. Thread Adapter
// ==========================================
sys_thread_t sys_thread_new(const char *name, lwip_thread_fn thread, void *arg, int stacksize, int prio) {
    sys_print("[lwIP OSAL] Spawning new lwIP system thread: ");
    sys_print(name);
    sys_print("\r\n");

    // Since our thread doesn't easily pass args through context yet, we'll store arg globally
    // or just let lwIP use global context. 
    // Wait, the lwIP tcpip_thread just needs its arg. We will cast it.
    
    uint32_t* thread_stack = new uint32_t[stacksize / sizeof(uint32_t)];
    
    // To support arg, we need to pass it. Our create_task only takes void(*)(void).
    // Let's assume lwIP handles it or we don't strictly need it for single netif setup.
    // Actually, tcpip_thread ignores arg or uses it for init done callback, but tcpip_init uses a global struct.
    
    Scheduler::instance().create_task(reinterpret_cast<void (*)()>(thread), thread_stack, stacksize,
        TaskPriority::High); // lwIP tcpip_thread 与 Shell 同级（High），通过时间片轮转共存
    
    return Scheduler::instance().get_current_tcb();
}
