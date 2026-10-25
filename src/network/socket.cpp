//SPDX-License-Identifier: MIT
//Author: Blayne Dennis

#include "socket.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <unistd.h>

namespace hce {
namespace detail {
namespace network {

void socket::close() {
    if(fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

std::error_code socket::set_nonblocking() {
    if(fd_ < 0) {
        return std::make_error_code(std::errc::bad_file_descriptor);
    }

    int flags = ::fcntl(fd_, F_GETFL, 0);

    if(flags < 0) {
        return std::error_code(errno, std::system_category());
    }

    if(::fcntl(fd_, F_SETFL, flags | O_NONBLOCK) < 0) {
        return std::error_code(errno, std::system_category());
    }

    return {};
}

std::error_code socket::bind(const sockaddr* addr, socklen_t len) {
    if(::bind(fd_, addr, len) < 0) {
        return std::error_code(errno, std::system_category());
    }

    return {};
}

std::error_code socket::listen(int backlog) {
    if(::listen(fd_, backlog) < 0) {
        return std::error_code(errno, std::system_category());
    }

    return {};
}

std::error_code socket::connect(const sockaddr* addr, socklen_t len) {
    if(::connect(fd_, addr, len) < 0) {
        if(errno == EINPROGRESS) {
            return {}; // non-blocking connect in progress
        }

        return std::error_code(errno, std::system_category());
    }

    return {};
}

ssize_t socket::send(std::span<const std::byte> data, std::error_code& ec) {
    ec.clear();
    ssize_t n = ::send(fd_, data.data(), data.size(), MSG_NOSIGNAL);

    if(n < 0) {
        if(errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0; // would block
        }

        ec = std::error_code(errno, std::system_category());
        return -1;
    }

    return n;
}

ssize_t socket::recv(std::span<std::byte> buf, std::error_code& ec) {
    ec.clear();
    ssize_t n = ::recv(fd_, buf.data(), buf.size(), 0);

    if(n < 0) {
        if(errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0; // would block
        }

        ec = std::error_code(errno, std::system_category());
        return -1;
    }

    return n;
}

socket socket::create_tcp() {
    int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);

    if(fd < 0) {
        return socket{};
    }

    socket s(fd);
    s.set_nonblocking();

    int one = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    return s;
}

socket socket::create_udp() {
    int fd = ::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);

    if(fd < 0) {
        return socket{};
    }

    socket s(fd);
    s.set_nonblocking();
    return s;
}

std::string to_string(const sockaddr* addr) {
    if(!addr) {
        return {};
    }

    char buf[INET6_ADDRSTRLEN] = {};

    if(addr->sa_family == AF_INET) {
        auto* a = reinterpret_cast<const sockaddr_in*>(addr);
        inet_ntop(AF_INET, &a->sin_addr, buf, sizeof(buf));
        return std::string(buf) + ":" + std::to_string(ntohs(a->sin_port));
    }

    if(addr->sa_family == AF_INET6) {
        auto* a = reinterpret_cast<const sockaddr_in6*>(addr);
        inet_ntop(AF_INET6, &a->sin6_addr, buf, sizeof(buf));
        return std::string("[") + buf + "]:" +
               std::to_string(ntohs(a->sin6_port));
    }

    return {};
}

} // network
} // detail
} // hce
