#ifndef AURORA_PROCFS_HPP
#define AURORA_PROCFS_HPP

#include "vfs.hpp"
#include "memory.hpp"
#include "task.hpp"
#include "../metrics/metrics.hpp"

// ProcFS 节点基类：只读，不支持写
class ProcNode : public VNode {
public:
    int write(const char* buf, int len, int offset, void* priv) override { return -1; } // 拒绝写入
};

// ==========================================
// 内存状态节点: /proc/meminfo
// ==========================================
class MemInfoNode : public ProcNode {
public:
    int read(char* buf, int len, int offset, void* priv) override {
        // 简单处理：仅在 offset 为 0 时生成数据，防止被无限读取
        if (offset > 0) return 0; 

        size_t free_mem = KernelHeap::instance().get_free_memory();
        size_t total_mem = KernelHeap::instance().get_total_memory();

        int pos = 0;
        // 裸机闭包：极简字符串拼接器
        auto append_str = [&](const char* s) { 
            while (*s && pos < len - 1) buf[pos++] = *s++; 
        };
        auto append_num = [&](uint32_t num) {
            char temp[16]; int i = 0;
            if (num == 0) { temp[i++] = '0'; }
            while (num > 0) { temp[i++] = (num % 10) + '0'; num /= 10; }
            while (i > 0 && pos < len - 1) buf[pos++] = temp[--i];
        };

        append_str("auroraOS Memory Info:\n");
        append_str("----------------------\n");
        append_str("MemTotal:  "); append_num(total_mem); append_str(" Bytes\n");
        append_str("MemFree:   "); append_num(free_mem); append_str(" Bytes\n");
        append_str("Defrag:    "); append_num(Metrics::get_heap_defrags()); append_str(" times\n");
        
        buf[pos] = '\0';
        return pos;
    }
};

// ==========================================
// 任务状态节点: /proc/taskinfo
// ==========================================
class TaskInfoNode : public ProcNode {
public:
    int read(char* buf, int len, int offset, void* priv) override {
        if (offset > 0) return 0;
        
        int pos = 0;
        auto append_str = [&](const char* s) { while (*s && pos < len - 1) buf[pos++] = *s++; };
        auto append_num = [&](uint32_t num) {
            char temp[16]; int i = 0;
            if (num == 0) { temp[i++] = '0'; }
            while (num > 0) { temp[i++] = (num % 10) + '0'; num /= 10; }
            while (i > 0 && pos < len - 1) buf[pos++] = temp[--i];
        };

        append_str("TID\tSTATE\tSLEEP_TICKS\n");
        append_str("-----------------------------\n");
        
        Scheduler& sched = Scheduler::instance();
        Arch::disable_interrupts(); // 保证并发读取 TCB 时任务队列不会被修改
        int count = sched.get_task_count();
        
        for (int i = 0; i < count; i++) {
            TaskControlBlock* tcb = sched.get_task(i);
            if (!tcb) continue;

            append_num(tcb->id);
            append_str("\t");
            
            // 状态解析
            if (tcb->state == TaskState::Running) append_str("RUN\t");
            else if (tcb->state == TaskState::Ready) append_str("RDY\t");
            else if (tcb->state == TaskState::Sleeping) append_str("SLP\t");
            else if (tcb->state == TaskState::Blocked_On_Notify) append_str("BNT\t");
            else if (tcb->state == TaskState::Suspended) append_str("SUS\t");
            else if (tcb->state == TaskState::Terminated) append_str("TRM\t");
            else append_str("UNK\t");

            append_num(tcb->sleep_ticks);
            append_str("\n");
        }
        Arch::enable_interrupts();
        
        buf[pos] = '\0';
        return pos;
    }
};

// ==========================================
// Latency 节点: /proc/latency
// ==========================================
class LatencyNode : public ProcNode {
public:
    int read(char* buf, int len, int offset, void* priv) override {
        if (offset > 0) return 0;
        int pos = 0;
        auto append_str = [&](const char* s) { while (*s && pos < len - 1) buf[pos++] = *s++; };
        auto append_num = [&](uint32_t num) {
            char temp[16]; int i = 0;
            if (num == 0) { temp[i++] = '0'; }
            while (num > 0) { temp[i++] = (num % 10) + '0'; num /= 10; }
            while (i > 0 && pos < len - 1) buf[pos++] = temp[--i];
        };

        append_str("ctx_switch avg="); append_num(Metrics::get_recorder(METRIC_CTX_SWITCH).get_avg_us());
        append_str("us max="); append_num(Metrics::get_recorder(METRIC_CTX_SWITCH).get_max_us());
        append_str("us p99="); append_num(Metrics::get_recorder(METRIC_CTX_SWITCH).get_p99_us());
        append_str("us count="); append_num(Metrics::get_recorder(METRIC_CTX_SWITCH).get_count());
        
        append_str("\nirq_latency avg="); append_num(Metrics::get_recorder(METRIC_IRQ_LATENCY).get_avg_us());
        append_str("us max="); append_num(Metrics::get_recorder(METRIC_IRQ_LATENCY).get_max_us());
        append_str("us p99="); append_num(Metrics::get_recorder(METRIC_IRQ_LATENCY).get_p99_us());
        append_str("us count="); append_num(Metrics::get_recorder(METRIC_IRQ_LATENCY).get_count());
        
        append_str("\nheap_64b avg="); append_num(Metrics::get_recorder(METRIC_HEAP_64B).get_avg_us());
        append_str("us max="); append_num(Metrics::get_recorder(METRIC_HEAP_64B).get_max_us());
        append_str("us p99="); append_num(Metrics::get_recorder(METRIC_HEAP_64B).get_p99_us());
        append_str("us count="); append_num(Metrics::get_recorder(METRIC_HEAP_64B).get_count());
        append_str("\n");
        buf[pos] = '\0';
        return pos;
    }
};

// ==========================================
// Power 节点: /proc/power
// ==========================================
class PowerNode : public ProcNode {
public:
    int read(char* buf, int len, int offset, void* priv) override {
        if (offset > 0) return 0;
        int pos = 0;
        auto append_str = [&](const char* s) { while (*s && pos < len - 1) buf[pos++] = *s++; };
        auto append_num = [&](uint32_t num) {
            char temp[16]; int i = 0;
            if (num == 0) { temp[i++] = '0'; }
            while (num > 0) { temp[i++] = (num % 10) + '0'; num /= 10; }
            while (i > 0 && pos < len - 1) buf[pos++] = temp[--i];
        };

        append_str("sleep_ratio "); append_num(Metrics::get_power_profiler().get_sleep_ratio());
        append_str("% sleep_count "); append_num(Metrics::get_power_profiler().get_sleep_count());
        // For dirty ratio, we stored percentage directly in the cycles field.
        // We will just use max_cycles since it is a raw value (unscaled by cycles_per_us).
        // Wait, max_us divides by cycles_per_us! We should just get average without dividing.
        uint32_t dirty_ratio = Metrics::get_recorder(METRIC_DIRTY_RATIO).get_count() > 0 ? 
            (Metrics::get_recorder(METRIC_DIRTY_RATIO).get_avg_us() * Arch::get_cycles_per_us()) : 0;
        append_str("\ndirty_ratio "); append_num(dirty_ratio);
        append_str("%\n");
        buf[pos] = '\0';
        return pos;
    }
};

// ==========================================
// Network 节点: /proc/net
// ==========================================
class NetNode : public ProcNode {
public:
    int read(char* buf, int len, int offset, void* priv) override {
        if (offset > 0) return 0;
        int pos = 0;
        auto append_str = [&](const char* s) { while (*s && pos < len - 1) buf[pos++] = *s++; };
        auto append_num = [&](uint32_t num) {
            char temp[16]; int i = 0;
            if (num == 0) { temp[i++] = '0'; }
            while (num > 0) { temp[i++] = (num % 10) + '0'; num /= 10; }
            while (i > 0 && pos < len - 1) buf[pos++] = temp[--i];
        };
        append_str("udp_drops "); append_num(Metrics::get_net_drops());
        append_str("\n");
        buf[pos] = '\0';
        return pos;
    }
};

// ==========================================
// Softbus 节点: /proc/softbus
// ==========================================
class SoftbusNode : public ProcNode {
public:
    int read(char* buf, int len, int offset, void* priv) override {
        if (offset > 0) return 0;
        int pos = 0;
        auto append_str = [&](const char* s) { while (*s && pos < len - 1) buf[pos++] = *s++; };
        auto append_num = [&](uint32_t num) {
            char temp[16]; int i = 0;
            if (num == 0) { temp[i++] = '0'; }
            while (num > 0) { temp[i++] = (num % 10) + '0'; num /= 10; }
            while (i > 0 && pos < len - 1) buf[pos++] = temp[--i];
        };
        append_str("registers "); append_num(Metrics::get_softbus_registers());
        append_str("\n");
        buf[pos] = '\0';
        return pos;
    }
};

#endif
