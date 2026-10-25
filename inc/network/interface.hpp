#pragma once
//SPDX-License-Identifier: MIT
//Author: Blayne Dennis

#include <functional>
#include <system_error>
#include <span>

#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/epoll.h>

namespace hce {
namespace interface {
namespace network {

struct socket {
    virtual ~socket() = 0;
    virtual int fd() const = 0;
    virtual bool valid() const = 0;

    virtual void close() = 0;

    // low-level helpers used by awaitables.
    virtual std::error_code set_nonblocking() = 0;
    virtual std::error_code bind(const sockaddr* addr, socklen_t len) = 0;
    virtual std::error_code listen(int backlog = SOMAXCONN) = 0;
    virtual std::error_code connect(const sockaddr* addr, socklen_t len) = 0;

    /*
     Returns bytes transferred or -1 + ec on error.

     EAGAIN / EWOULDBLOCK means the caller should re-register interest.
     */
    virtual ssize_t send(std::span<const std::byte> data, std::error_code& ec) = 0;
    virtual ssize_t recv(std::span<std::byte> buf, std::error_code& ec) = 0;
}; // socket

struct backend {
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

    virtual ~backend() = 0;

    /**
     @brief Start the IO threads. Safe to call once.
     */
    virtual void start() = 0;

    /**
     @brief Stop the IO threads and wait for them to exit.
     */
    virtual void stop() = 0;

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
    virtual op_id register_op(
            int fd,
            interest i,
            void* user,
            completion_fn complete) = 0;

    /**
     @brief Cancel a previously registered operation.

     If the operation has not yet completed, its completion will be
     invoked with an operation_canceled error.
     */
    virtual bool cancel(op_id id) = 0;

    /**
     @brief Assign a file descriptor to an IO thread (for affinity).

     Normally called automatically on first registration. Exposed so
     the caller can force affinity before the first operation if desired.
     */
    virtual void assign(int fd) = 0;

    /**
     @return Number of IO threads.
     */
    virtual size_t thread_count() const = 0;
}; // backend

} // network
} // interface
} // hce
