#ifndef IPC_HPP
#define IPC_HPP

#include <stdint.h>

struct TaskControlBlock;

namespace auroraos {
namespace kernel {

enum class IpcState : uint8_t {
    Ready = 0,
    Receiving = 1,
    ReplyBlocked = 2,
    Sending = 3
};

class Endpoint {
public:
    Endpoint() : send_head_(nullptr), send_tail_(nullptr), recv_head_(nullptr), recv_tail_(nullptr) {}

    // Synchronous IPC Call
    void call(TaskControlBlock* sender, void* msg, uint32_t len, void* reply_buf, uint32_t max_reply_len);

    // Synchronous IPC Receive
    void receive(TaskControlBlock* receiver, void* msg_buf, uint32_t max_len);

    // Synchronous IPC Reply
    static void reply(TaskControlBlock* receiver, uint32_t sender_id, void* reply_msg, uint32_t len);

private:
    void enqueue_sender(TaskControlBlock* tcb);
    TaskControlBlock* dequeue_sender();
    
    void enqueue_receiver(TaskControlBlock* tcb);
    TaskControlBlock* dequeue_receiver();

    TaskControlBlock* send_head_;
    TaskControlBlock* send_tail_;
    
    TaskControlBlock* recv_head_;
    TaskControlBlock* recv_tail_;
};

} // namespace kernel
} // namespace auroraos

#endif // IPC_HPP
