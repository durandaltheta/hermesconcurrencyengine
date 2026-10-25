#pragma once
//SPDX-License-Identifier: MIT
//Author: Blayne Dennis

#include <cstdint>
#include <span>
#include <string>
#include <system_error>

#include <sys/socket.h>

#include "network/interface.hpp"

namespace hce {
namespace detail {
namespace network {

/**
 @brief Thin RAII wrapper around a non-blocking POSIX socket.

 This is the object awaitables hold. The backend only cares about the raw file 
 descriptor.
 */
struct socket : public hce::interface::network::socket {
    socket() = default;
    explicit socket(int fd) : fd_(fd) {}
    virtual ~socket();

    socket(const socket&) = delete;
    socket(socket&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; }

    socket& operator=(const socket&) = delete;

    socket& operator=(socket&& other) noexcept {
        if(this != &other) {
            close();
            fd_ = other.fd_;
            other.fd_ = -1;
        }

        return *this;
    }

    int fd() const { return fd_; }
    bool valid() const { return fd_ >= 0; }

    void close();

    // low-level helpers used by awaitables.
    std::error_code set_nonblocking();
    std::error_code bind(const sockaddr* addr, socklen_t len);
    std::error_code listen(int backlog = SOMAXCONN);
    std::error_code connect(const sockaddr* addr, socklen_t len);

    /*
     Returns bytes transferred or -1 + ec on error.

     EAGAIN / EWOULDBLOCK means the caller should re-register interest.
     */
    ssize_t send(std::span<const std::byte> data, std::error_code& ec);
    ssize_t recv(std::span<std::byte> buf, std::error_code& ec);

    static socket create_tcp();
    static socket create_udp();

    static std::string to_string(const sockaddr* addr);

private:
    int fd_ = -1;
};

/**
 @brief Helper to format an IPv4 or IPv6 address into a string.
 */
std::string to_string(const sockaddr* addr);

} // network
} // detail
} // hce
