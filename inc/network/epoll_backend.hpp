#pragma once
//SPDX-License-Identifier: MIT
//Author: Blayne Dennis

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <vector>

#include <sys/epoll.h>
#include <unistd.h>

namespace hce {
namespace detail {
namespace network {
namespace backend {

/**
 @brief Interest mask for a registered operation.
 */
enum class interest : uint32_t {
    read  = EPOLLIN,
    write = EPOLLOUT,
    both  = EPOLLIN | EPOLLOUT
};

/**
 @brief Completion callback invoked from an IO thread.

 @param fd      The file descriptor that became ready.
 @param ec      Error code (success if empty).
 @param events  Raw epoll events that fired.
 @param user    User data supplied at registration.
 */
using completion_fn = std::function<void(
    int fd,
    std::error_code ec,
    uint32_t events,
    void* user)>;

/**
 @brief Unique identifier for a registered operation (for cancellation).
 */
using op_id = uint64_t;

/**
 @brief Multi-threaded edge-triggered epoll backend.

 Each IO thread owns its own epoll instance. Sockets are assigned to a
 thread via a simple hash so that all operations on a given fd stay on
 the same thread (no cross-thread locking on the hot path).

 Completions are delivered on the IO thread that owns the socket. The
 caller is expected to post the resume of any waiting coroutine onto a
 Hermes scheduler from inside the completion function.
 */
class epoll_backend {
public:
    explicit epoll_backend(size_t thread_count = 1);
    ~epoll_backend();

    epoll_backend(const epoll_backend&) = delete;
    epoll_backend& operator=(const epoll_backend&) = delete;

    /**
     @brief Start the IO threads. Safe to call once.
     */
    void start();

    /**
     @brief Stop the IO threads and wait for them to exit.
     */
    void stop();

    /**
     @brief Register interest on a non-blocking socket.

     The completion is invoked exactly once when the interest becomes
     ready (or an error occurs). The caller must re-register if more
     data is expected (edge-triggered).

     @param fd       Non-blocking file descriptor.
     @param i        Interest mask.
     @param user     Opaque user pointer passed to the completion.
     @param complete Completion function (called from the owning IO thread).
     @return         Operation id that can be used for cancellation, or 0 on failure.
     */
    op_id register_op(int fd,
                      interest i,
                      void* user,
                      completion_fn complete);

    /**
     @brief Cancel a previously registered operation.

     If the operation has not yet completed, its completion will be
     invoked with an operation_canceled error.
     */
    bool cancel(op_id id);

    /**
     @brief Assign a file descriptor to an IO thread (for affinity).

     Normally called automatically on first registration. Exposed so
     the caller can force affinity before the first operation if desired.
     */
    void assign(int fd);

    /**
     @return Number of IO threads.
     */
    size_t thread_count() const { return thread_count_; }

private:
    struct op {
        op_id id = 0;
        int fd = -1;
        interest interest_ = interest::read;
        void* user = nullptr;
        completion_fn complete;
        bool cancelled = false;
    };

    struct thread_state {
        int epoll_fd = -1;
        int wake_fd = -1;               // eventfd for stopping / waking
        std::thread thread;
        std::atomic<bool> running{false};

        // Protected by mutex only during registration / cancellation.
        // The IO thread itself never takes the mutex while in epoll_wait.
        std::mutex mutex;
        std::unordered_map<op_id, std::shared_ptr<op>> ops_by_id;
        std::unordered_map<int, std::shared_ptr<op>> ops_by_fd;
    };

    void io_thread_main(size_t index);
    thread_state& thread_for(int fd);
    void wake(thread_state& ts);
    static void set_nonblocking(int fd);

    const size_t thread_count_;
    std::vector<std::unique_ptr<thread_state>> threads_;
    std::atomic<op_id> next_id_{1};
    std::atomic<bool> started_{false};
    std::atomic<bool> stopped_{false};
};

} // backend
} // network
} // detail
} // hce
