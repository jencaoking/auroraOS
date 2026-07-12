// =============================================================================
// kernel_stubs.cpp — Linker stubs for kernel symbols undefined on host
//
// Provides definitions for symbols declared in kernel/task.hpp with external
// linkage that are normally implemented in firmware-specific .cpp files:
//  - g_current_tcb_ptr / g_next_tcb_ptr  (apps/kernel.cpp on target)
//  - frame_scheduler_is_task_allowed     (apps/kernel.cpp on target)
//
// We include host_prelude.hpp first to resolve the POSIX signal macro
// conflict before pulling in task.hpp.
// =============================================================================

// Force-include the signal macro undef before pulling in task.hpp.
// (The -include CMake flag handles this for test_*.cpp files;  here we
// must do it explicitly because this file is compiled with the same flags.)
#include <signal.h>
#ifdef SIGINT
#  undef SIGINT
#endif
#ifdef SIGKILL
#  undef SIGKILL
#endif
#ifdef SIGALRM
#  undef SIGALRM
#endif
#ifdef SIGUSR1
#  undef SIGUSR1
#endif

#include "task.hpp"  // Provides TaskControlBlock, Scheduler (resolved via stubs/ path)

// ---------------------------------------------------------------------------
// Global TCB volatile pointers (extern "C" declarations in task.hpp).
// Scheduler::schedule() writes these before calling trigger_context_switch();
// on the host trigger_context_switch() is a no-op stub, so these pointers
// are never dereferenced during unit tests.
// ---------------------------------------------------------------------------
extern "C" {

TaskControlBlock* volatile g_current_tcb_ptr = nullptr;
TaskControlBlock* volatile g_next_tcb_ptr    = nullptr;

// ---------------------------------------------------------------------------
// frame_scheduler_is_task_allowed — C-linkage stub
//
// Returns true so Scheduler::schedule() never filters tasks based on
// frame budget during unit tests.
// ---------------------------------------------------------------------------
bool frame_scheduler_is_task_allowed(uint8_t /*priority*/) {
    return true;
}

}  // extern "C"

namespace Arch {
    void disable_systick() {}
    void enable_systick() {}
    void start_wakeup_timer(uint32_t /*ticks*/) {}
    uint32_t stop_wakeup_timer() { return 0; }
}
