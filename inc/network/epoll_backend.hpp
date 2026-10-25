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

#include "network/interface.hpp"

namespace hce {
namespace detail {
namespace network {

/**
 @brief Multi-threaded edge-triggered epoll backend.

 Each IO thread owns its own epoll instance. Sockets are assigned to a
 thread via a simple hash so that all operations on a given fd stay on
 the same thread (no cross-thread locking on the hot path).

 Completions are delivered on the IO thread that owns the socket. The
 caller is expected to post the resume of any waiting coroutine onto a
 Hermes scheduler from inside the completion function.
 */
struct epoll_backend : public hce::interface::network::backend {
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
    hce::interface::network::backend::op_id register_op(int fd,
                      hce::interface::network::backend::interest i,
                      void* user,
                      hce::interface::network::backend::completion_fn complete);

    /**
     @brief Cancel a previously registered operation.

     If the operation has not yet completed, its completion will be
     invoked with an operation_canceled error.
     */
    bool cancel(hce::interface::network::backend::op_id id);

    /**
     @brief Assign a file descriptor to an IO thread (for affinity).

     Normally called automatically on first registration. Exposed so
     the caller can force affinity before the first operation if desired.
     */
    void assign(int fd);

    /**
     @return Number of IO threads.
     */
    size_t thread_count() const;

private:
    struct op {
        hce::interface::network::backend::op_id id = 0;
        int fd = -1;
        hce::interface::network::backend::interest interest_ = interest::read;
        void* user = nullptr;
        hce::interface::network::backend::completion_fn complete;
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
        std::unordered_map<hce::interface::network::backend::op_id, std::shared_ptr<op>> ops_by_id;
        std::unordered_map<int, std::shared_ptr<op>> ops_by_fd;
    };

    void io_thread_main(size_t index);
    thread_state& thread_for(int fd);
    ssize_t wake(thread_state& ts);
    static void set_nonblocking(int fd);

    const size_t thread_count_;
    std::vector<std::unique_ptr<thread_state>> threads_;
    std::atomic<hce::interface::network::backend::op_id> next_id_{1};
    std::atomic<bool> started_{false};
    std::atomic<bool> stopped_{false};
};

} // network
} // detail
} // hce
