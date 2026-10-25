//SPDX-License-Identifier: MIT
//Author: Blayne Dennis

#include "network.hpp"

#include <arpa/inet.h>
#include <netdb.h>

namespace hce {
namespace network {

// -----------------------------------------------------------------------------
// connection
// -----------------------------------------------------------------------------

network::connection::connection(connection&& other) noexcept
    : socket(std::move(other.socket))
{}

network::connection&
network::connection::operator=(connection&& other) noexcept {
    if(this != &other) {
        socket = std::move(other.socket);
    }
    return *this;
}

network::connection::~connection() {
    close();
}

void network::connection::close() {
    socket.close();
}

bool network::connection::valid() const {
    return socket.valid();
}

int network::connection::fd() const {
    return socket.fd();
}

// ---- send -------------------------------------------------------------------

struct send_awaitable :
    public hce::scheduler::reschedule<
        hce::awaitable::lockable<
            hce::spinlock,
            hce::awt<std::error_code>::interface>>
{
    send_awaitable(detail::network::socket& s,
                   std::span<const std::byte> d) :
        hce::scheduler::reschedule<
            hce::awaitable::lockable<
                hce::spinlock,
                hce::awt<std::error_code>::interface>>(
                    slk_,
                    hce::awaitable::await::policy::defer,
                    hce::awaitable::resume::policy::lock),
        sock(s),
        data(d)
    {}

    bool on_ready() override { return ready_; }

    void on_suspend() override {
        auto& backend = hce::service<network>::get().backend;
        backend.register_op(
            sock.fd(),
            detail::network::backend::interest::write,
            this,
            [](int, std::error_code ec, uint32_t, void* user) {
                auto* self = static_cast<send_awaitable*>(user);
                // Package the result so on_resume can pick it up.
                // We complete immediately from the IO thread via resume().
                auto* result = new std::error_code(ec);
                if(!ec) {
                    std::error_code io_ec;
                    ssize_t n = self->sock.send(self->data, io_ec);
                    if(n < 0) {
                        *result = io_ec;
                    } else if(n == 0) {
                        *result = std::make_error_code(
                            std::errc::resource_unavailable_try_again);
                    } else {
                        self->bytes = static_cast<size_t>(n);
                    }
                }
                self->resume(result);
            });
    }

    void on_resume(void* m) override {
        auto* ec = static_cast<std::error_code*>(m);
        result = *ec;
        delete ec;
        ready_ = true;
    }

    std::error_code get_result() override { return result; }

    hce::spinlock slk_;
    detail::network::socket& sock;
    std::span<const std::byte> data;
    bool ready_ = false;
    std::error_code result;
    size_t bytes = 0;
};

hce::awt<std::error_code>
network::connection::send(std::span<const std::byte> data) {
    return hce::awt<std::error_code>(new send_awaitable(socket, data));
}

// ---- send_all ---------------------------------------------------------------

struct send_all_awaitable :
    public hce::scheduler::reschedule<
        hce::awaitable::lockable<
            hce::spinlock,
            hce::awt<std::error_code>::interface>>
{
    send_all_awaitable(detail::network::socket& s,
                       std::span<const std::byte> d) :
        hce::scheduler::reschedule<
            hce::awaitable::lockable<
                hce::spinlock,
                hce::awt<std::error_code>::interface>>(
                    slk_,
                    hce::awaitable::await::policy::defer,
                    hce::awaitable::resume::policy::lock),
        sock(s),
        data(d)
    {}

    bool on_ready() override { return ready_; }

    void on_suspend() override {
        try_send();
    }

    void try_send() {
        while (offset < data.size()) {
            std::error_code ec;
            ssize_t n = sock.send(data.subspan(offset), ec);
            if(n > 0) {
                offset += static_cast<size_t>(n);
                continue;
            }
            if(!ec || ec == std::errc::resource_unavailable_try_again) {
                auto& backend = hce::service<network>::get().backend;
                backend.register_op(
                    sock.fd(),
                    detail::network::backend::interest::write,
                    this,
                    [](int, std::error_code ec, uint32_t, void* user) {
                        auto* self = static_cast<send_all_awaitable*>(user);
                        if(ec) {
                            self->resume(new std::error_code(ec));
                            return;
                        }
                        self->try_send();
                    });
                return;
            }
            resume(new std::error_code(ec));
            return;
        }
        resume(new std::error_code{});
    }

    void on_resume(void* m) override {
        auto* ec = static_cast<std::error_code*>(m);
        result = *ec;
        delete ec;
        ready_ = true;
    }

    std::error_code get_result() override { return result; }

    hce::spinlock slk_;
    detail::network::socket& sock;
    std::span<const std::byte> data;
    size_t offset = 0;
    bool ready_ = false;
    std::error_code result;
};

hce::awt<std::error_code>
network::connection::send_all(std::span<const std::byte> data) {
    return hce::awt<std::error_code>(new send_all_awaitable(socket, data));
}

// ---- recv -------------------------------------------------------------------

struct recv_awaitable :
    public hce::scheduler::reschedule<
        hce::awaitable::lockable<
            hce::spinlock,
            hce::awt<std::expected<size_t, std::error_code>>::interface>>
{
    recv_awaitable(detail::network::socket& s, std::span<std::byte> b) :
        hce::scheduler::reschedule<
            hce::awaitable::lockable<
                hce::spinlock,
                hce::awt<std::expected<size_t, std::error_code>>::interface>>(
                    slk_,
                    hce::awaitable::await::policy::defer,
                    hce::awaitable::resume::policy::lock),
        sock(s),
        buf(b)
    {}

    bool on_ready() override { return ready_; }

    void on_suspend() override {
        auto& backend = hce::service<network>::get().backend;
        backend.register_op(
            sock.fd(),
            detail::network::backend::interest::read,
            this,
            [](int, std::error_code ec, uint32_t, void* user) {
                auto* self = static_cast<recv_awaitable*>(user);
                if(ec) {
                    self->resume(new std::expected<size_t, std::error_code>(
                        std::unexpected(ec)));
                } else {
                    std::error_code io_ec;
                    ssize_t n = self->sock.recv(self->buf, io_ec);
                    if(n < 0) {
                        self->resume(new std::expected<size_t, std::error_code>(
                            std::unexpected(io_ec)));
                    } else {
                        self->resume(new std::expected<size_t, std::error_code>(
                            static_cast<size_t>(n)));
                    }
                }
            });
    }

    void on_resume(void* m) override {
        auto* r = static_cast<std::expected<size_t, std::error_code>*>(m);
        result = std::move(*r);
        delete r;
        ready_ = true;
    }

    std::expected<size_t, std::error_code> get_result() override {
        return result;
    }

    hce::spinlock slk_;
    detail::network::socket& sock;
    std::span<std::byte> buf;
    bool ready_ = false;
    std::expected<size_t, std::error_code> result;
};

hce::awt<std::expected<size_t, std::error_code>>
network::connection::recv(std::span<std::byte> data) {
    return hce::awt<std::expected<size_t, std::error_code>>(
        new recv_awaitable(socket, data));
}

// ---- recv_all ---------------------------------------------------------------

struct recv_all_awaitable :
    public hce::scheduler::reschedule<
        hce::awaitable::lockable<
            hce::spinlock,
            hce::awt<std::expected<size_t, std::error_code>>::interface>>
{
    recv_all_awaitable(detail::network::socket& s, std::span<std::byte> b) :
        hce::scheduler::reschedule<
            hce::awaitable::lockable<
                hce::spinlock,
                hce::awt<std::expected<size_t, std::error_code>>::interface>>(
                    slk_,
                    hce::awaitable::await::policy::defer,
                    hce::awaitable::resume::policy::lock),
        sock(s),
        buf(b)
    {}

    bool on_ready() override { return ready_; }

    void on_suspend() override {
        try_recv();
    }

    void try_recv() {
        while (offset < buf.size()) {
            std::error_code ec;
            ssize_t n = sock.recv(buf.subspan(offset), ec);
            if(n > 0) {
                offset += static_cast<size_t>(n);
                continue;
            }
            if(n == 0 && !ec) {
                // EOF
                resume(new std::expected<size_t, std::error_code>(offset));
                return;
            }
            if(!ec || ec == std::errc::resource_unavailable_try_again) {
                auto& backend = hce::service<network>::get().backend;
                backend.register_op(
                    sock.fd(),
                    detail::network::backend::interest::read,
                    this,
                    [](int, std::error_code ec, uint32_t, void* user) {
                        auto* self = static_cast<recv_all_awaitable*>(user);
                        if(ec) {
                            self->resume(
                                new std::expected<size_t, std::error_code>(
                                    std::unexpected(ec)));
                            return;
                        }
                        self->try_recv();
                    });
                return;
            }
            resume(new std::expected<size_t, std::error_code>(
                std::unexpected(ec)));
            return;
        }
        resume(new std::expected<size_t, std::error_code>(offset));
    }

    void on_resume(void* m) override {
        auto* r = static_cast<std::expected<size_t, std::error_code>*>(m);
        result = std::move(*r);
        delete r;
        ready_ = true;
    }

    std::expected<size_t, std::error_code> get_result() override {
        return result;
    }

    hce::spinlock slk_;
    detail::network::socket& sock;
    std::span<std::byte> buf;
    size_t offset = 0;
    bool ready_ = false;
    std::expected<size_t, std::error_code> result;
};

hce::awt<std::expected<size_t, std::error_code>>
network::connection::recv_all(std::span<std::byte> data) {
    return hce::awt<std::expected<size_t, std::error_code>>(
        new recv_all_awaitable(socket, data));
}

// -----------------------------------------------------------------------------
// acceptor
// -----------------------------------------------------------------------------

network::acceptor::acceptor(acceptor&& other) noexcept
    : socket(std::move(other.socket))
{}

network::acceptor&
network::acceptor::operator=(acceptor&& other) noexcept {
    if(this != &other) {
        socket = std::move(other.socket);
    }
    return *this;
}

network::acceptor::~acceptor() {
    close();
}

void network::acceptor::close() {
    socket.close();
}

int network::acceptor::fd() const {
    return socket.fd();
}

struct accept_awaitable :
    public hce::scheduler::reschedule<
        hce::awaitable::lockable<
            hce::spinlock,
            hce::awt<std::expected<network::connection,
                                   std::error_code>>::interface>>
{
    explicit accept_awaitable(detail::network::socket& s) :
        hce::scheduler::reschedule<
            hce::awaitable::lockable<
                hce::spinlock,
                hce::awt<std::expected<network::connection,
                                       std::error_code>>::interface>>(
                    slk_,
                    hce::awaitable::await::policy::defer,
                    hce::awaitable::resume::policy::lock),
        sock(s)
    {}

    bool on_ready() override { return ready_; }

    void on_suspend() override {
        auto& backend = hce::service<network>::get().backend;
        backend.register_op(
            sock.fd(),
            detail::network::backend::interest::read,
            this,
            [](int, std::error_code ec, uint32_t, void* user) {
                auto* self = static_cast<accept_awaitable*>(user);
                if(ec) {
                    self->resume(
                        new std::expected<network::connection, std::error_code>(
                            std::unexpected(ec)));
                    return;
                }

                sockaddr_storage addr{};
                socklen_t len = sizeof(addr);
                int cfd = ::accept4(self->sock.fd(),
                                    reinterpret_cast<sockaddr*>(&addr),
                                    &len,
                                    SOCK_NONBLOCK | SOCK_CLOEXEC);
                if(cfd < 0) {
                    self->resume(
                        new std::expected<network::connection, std::error_code>(
                            std::unexpected(std::error_code(
                                errno, std::system_category()))));
                } else {
                    network::connection conn;
                    conn.socket = detail::network::socket(cfd);
                    self->resume(
                        new std::expected<network::connection, std::error_code>(
                            std::move(conn)));
                }
            });
    }

    void on_resume(void* m) override {
        auto* r = static_cast<
            std::expected<network::connection, std::error_code>*>(m);
        result = std::move(*r);
        delete r;
        ready_ = true;
    }

    std::expected<network::connection, std::error_code>
    get_result() override {
        return std::move(result);
    }

    hce::spinlock slk_;
    detail::network::socket& sock;
    bool ready_ = false;
    std::expected<network::connection, std::error_code> result;
};

hce::awt<std::expected<network::connection, std::error_code>>
network::acceptor::accept() {
    return hce::awt<std::expected<network::connection, std::error_code>>(
        new accept_awaitable(socket));
}

// -----------------------------------------------------------------------------
// network service
// -----------------------------------------------------------------------------

network::network()
    : backend(hce::config::network::thread_count())
{
    backend.start();
}

network::~network() {
    backend.stop();
}

std::string network::info_name() {
    return "hce::network";
}

std::string network::name() const {
    return network::info_name();
}

// ---- connect -------------------------------------------------------------

struct connect_awaitable :
    public hce::scheduler::reschedule<
        hce::awaitable::lockable<
            hce::spinlock,
            hce::awt<std::expected<network::connection,
                                   std::error_code>>::interface>>
{
    explicit connect_awaitable(const network::address& a) :
        hce::scheduler::reschedule<
            hce::awaitable::lockable<
                hce::spinlock,
                hce::awt<std::expected<network::connection,
                                       std::error_code>>::interface>>(
                    slk_,
                    hce::awaitable::await::policy::defer,
                    hce::awaitable::resume::policy::lock),
        addr(a)
    {}

    bool on_ready() override { return ready_; }

    void on_suspend() override {
        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* res = nullptr;
        int rc = ::getaddrinfo(addr.host.c_str(),
                               std::to_string(addr.port).c_str(),
                               &hints, &res);
        if(rc != 0) {
            resume(new std::expected<network::connection, std::error_code>(
                std::unexpected(std::make_error_code(
                    std::errc::host_unreachable))));
            return;
        }

        int fd = ::socket(res->ai_family,
                          res->ai_socktype | SOCK_NONBLOCK | SOCK_CLOEXEC,
                          res->ai_protocol);
        if(fd < 0) {
            ::freeaddrinfo(res);
            resume(new std::expected<network::connection, std::error_code>(
                std::unexpected(std::error_code(errno,
                                                std::system_category()))));
            return;
        }

        pending.socket = detail::network::socket(fd);

        int cr = ::connect(fd, res->ai_addr, res->ai_addrlen);
        ::freeaddrinfo(res);

        if(cr == 0) {
            resume(new std::expected<network::connection, std::error_code>(
                std::move(pending)));
            return;
        }

        if(errno != EINPROGRESS) {
            resume(new std::expected<network::connection, std::error_code>(
                std::unexpected(std::error_code(errno,
                                                std::system_category()))));
            return;
        }

        auto& backend = hce::service<network>::get().backend;
        backend.register_op(
            fd,
            detail::network::backend::interest::write,
            this,
            [](int fd, std::error_code ec, uint32_t, void* user) {
                auto* self = static_cast<connect_awaitable*>(user);
                if(ec) {
                    self->resume(
                        new std::expected<network::connection, std::error_code>(
                            std::unexpected(ec)));
                    return;
                }

                int err = 0;
                socklen_t len = sizeof(err);
                if(::getsockopt(fd, SOL_SOCKET, SO_ERROR,
                                 &err, &len) < 0 || err != 0) {
                    self->resume(
                        new std::expected<network::connection, std::error_code>(
                            std::unexpected(std::error_code(
                                err ? err : errno,
                                std::system_category()))));
                } else {
                    self->resume(
                        new std::expected<network::connection, std::error_code>(
                            std::move(self->pending)));
                }
            });
    }

    void on_resume(void* m) override {
        auto* r = static_cast<
            std::expected<network::connection, std::error_code>*>(m);
        result = std::move(*r);
        delete r;
        ready_ = true;
    }

    std::expected<network::connection, std::error_code>
    get_result() override {
        return std::move(result);
    }

    hce::spinlock slk_;
    network::address addr;
    network::connection pending;
    bool ready_ = false;
    std::expected<network::connection, std::error_code> result;
};

hce::awt<std::expected<network::connection, std::error_code>>
network::connect(const address& addr) {
    return hce::awt<std::expected<network::connection, std::error_code>>(
        new connect_awaitable(addr));
}

// ---- listen --------------------------------------------------------------

std::expected<network::acceptor, std::error_code>
network::listen(const address& addr, int backlog) {
    int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if(fd < 0) {
        return std::unexpected(std::error_code(errno, std::system_category()));
    }

    int one = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(addr.port);

    if(addr.host.empty() || addr.host == "0.0.0.0") {
        sa.sin_addr.s_addr = INADDR_ANY;
    } else if(::inet_pton(AF_INET, addr.host.c_str(), &sa.sin_addr) != 1) {
        ::close(fd);
        return std::unexpected(
            std::make_error_code(std::errc::invalid_argument));
    }

    if(::bind(fd, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) < 0) {
        ::close(fd);
        return std::unexpected(std::error_code(errno, std::system_category()));
    }

    if(::listen(fd, backlog) < 0) {
        ::close(fd);
        return std::unexpected(std::error_code(errno, std::system_category()));
    }

    acceptor a;
    a.socket = detail::network::socket(fd);
    return a;
}

} // network
} // hce
