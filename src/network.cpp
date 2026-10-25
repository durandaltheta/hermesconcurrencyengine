//SPDX-License-Identifier: MIT
//Author: Blayne Dennis

#include "network.hpp"

#include <arpa/inet.h>
#include <netdb.h>

namespace hce {

// ---------------------------------------------------------------------------
// Helpers — result objects that cross the resume boundary.
// ---------------------------------------------------------------------------

struct error_result {
    std::error_code ec;
};

struct size_result {
    hce::expected<size_t, std::error_code> value;
};

struct connection_result {
    hce::expected<network::connection, std::error_code> value;
};

// ---------------------------------------------------------------------------
// connection
// ---------------------------------------------------------------------------

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

// ---- send ----------------------------------------------------------------

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
        auto& backend = hce::service<network>::get().get_backend();
        backend.register_op(
            sock.fd(),
            hce::interface::network::backend::interest::write,
            this,
            [](int, std::error_code ec, uint32_t, void* user) {
                auto* self = static_cast<send_awaitable*>(user);
                auto* r = new error_result;
                r->ec = ec;

                if(!ec) {
                    std::error_code io_ec;
                    ssize_t n = self->sock.send(self->data, io_ec);
                    if(n < 0) {
                        r->ec = io_ec;
                    } else if(n == 0) {
                        r->ec = std::make_error_code(
                            std::errc::resource_unavailable_try_again);
                    } else {
                        self->bytes = static_cast<size_t>(n);
                    }
                }

                self->resume(r);
            });
    }

    void on_resume(void* m) override {
        auto* r = static_cast<error_result*>(m);
        result = r->ec;
        delete r;
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

// ---- send_all ------------------------------------------------------------

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
        // Kick off the first attempt. We never call resume from this stack.
        continue_send();
    }

    void continue_send() {
        while(offset < data.size()) {
            std::error_code ec;
            ssize_t n = sock.send(data.subspan(offset), ec);
            if(n > 0) {
                offset += static_cast<size_t>(n);
                continue;
            }

            if(!ec || ec == std::errc::resource_unavailable_try_again) {
                // Need writability. Register and return; completion will
                // call continue_send again (or resume on hard error).
                auto& backend = hce::service<network>::get().get_backend();
                backend.register_op(
                    sock.fd(),
                    hce::interface::network::backend::interest::write,
                    this,
                    [](int, std::error_code ec, uint32_t, void* user) {
                        auto* self = static_cast<send_all_awaitable*>(user);
                        if(ec) {
                            auto* r = new error_result;
                            r->ec = ec;
                            self->resume(r);
                            return;
                        }
                        self->continue_send();
                    });
                return;
            }

            // Hard error — finish.
            auto* r = new error_result;
            r->ec = ec;
            resume(r);
            return;
        }

        // All bytes sent.
        auto* r = new error_result;
        r->ec = {};
        resume(r);
    }

    void on_resume(void* m) override {
        auto* r = static_cast<error_result*>(m);
        result = r->ec;
        delete r;
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

// ---- recv ----------------------------------------------------------------

struct recv_awaitable :
    public hce::scheduler::reschedule<
        hce::awaitable::lockable<
            hce::spinlock,
            hce::awt<hce::expected<size_t, std::error_code>>::interface>>
{
    recv_awaitable(detail::network::socket& s, std::span<std::byte> b) :
        hce::scheduler::reschedule<
            hce::awaitable::lockable<
                hce::spinlock,
                hce::awt<hce::expected<size_t, std::error_code>>::interface>>(
                    slk_,
                    hce::awaitable::await::policy::defer,
                    hce::awaitable::resume::policy::lock),
        sock(s),
        buf(b)
    {}

    bool on_ready() override { return ready_; }

    void on_suspend() override {
        auto& backend = hce::service<network>::get().get_backend();
        backend.register_op(
            sock.fd(),
            hce::interface::network::backend::interest::read,
            this,
            [](int, std::error_code ec, uint32_t, void* user) {
                auto* self = static_cast<recv_awaitable*>(user);
                auto* r = new size_result;
                if(ec) {
                    r->value = std::unexpected(ec);
                } else {
                    std::error_code io_ec;
                    ssize_t n = self->sock.recv(self->buf, io_ec);
                    if(n < 0) {
                        r->value = std::unexpected(io_ec);
                    } else {
                        r->value = static_cast<size_t>(n);
                    }
                }
                self->resume(r);
            });
    }

    void on_resume(void* m) override {
        auto* r = static_cast<size_result*>(m);
        result = std::move(r->value);
        delete r;
        ready_ = true;
    }

    hce::expected<size_t, std::error_code> get_result() override {
        return result;
    }

    hce::spinlock slk_;
    detail::network::socket& sock;
    std::span<std::byte> buf;
    bool ready_ = false;
    hce::expected<size_t, std::error_code> result;
};

hce::awt<hce::expected<size_t, std::error_code>>
network::connection::recv(std::span<std::byte> data) {
    return hce::awt<hce::expected<size_t, std::error_code>>(
        new recv_awaitable(socket, data));
}

// ---- recv_all ------------------------------------------------------------

struct recv_all_awaitable :
    public hce::scheduler::reschedule<
        hce::awaitable::lockable<
            hce::spinlock,
            hce::awt<hce::expected<size_t, std::error_code>>::interface>>
{
    recv_all_awaitable(detail::network::socket& s, std::span<std::byte> b) :
        hce::scheduler::reschedule<
            hce::awaitable::lockable<
                hce::spinlock,
                hce::awt<hce::expected<size_t, std::error_code>>::interface>>(
                    slk_,
                    hce::awaitable::await::policy::defer,
                    hce::awaitable::resume::policy::lock),
        sock(s),
        buf(b)
    {}

    bool on_ready() override { return ready_; }

    void on_suspend() override {
        continue_recv();
    }

    void continue_recv() {
        while(offset < buf.size()) {
            std::error_code ec;
            ssize_t n = sock.recv(buf.subspan(offset), ec);
            if(n > 0) {
                offset += static_cast<size_t>(n);
                continue;
            }
            if(n == 0 && !ec) {
                // EOF — finish with what we have.
                auto* r = new size_result;
                r->value = offset;
                resume(r);
                return;
            }
            if(!ec || ec == std::errc::resource_unavailable_try_again) {
                auto& backend = hce::service<network>::get().get_backend();
                backend.register_op(
                    sock.fd(),
                    hce::interface::network::backend::interest::read,
                    this,
                    [](int, std::error_code ec, uint32_t, void* user) {
                        auto* self = static_cast<recv_all_awaitable*>(user);
                        if(ec) {
                            auto* r = new size_result;
                            r->value = std::unexpected(ec);
                            self->resume(r);
                            return;
                        }
                        self->continue_recv();
                    });
                return;
            }
            auto* r = new size_result;
            r->value = std::unexpected(ec);
            resume(r);
            return;
        }
        auto* r = new size_result;
        r->value = offset;
        resume(r);
    }

    void on_resume(void* m) override {
        auto* r = static_cast<size_result*>(m);
        result = std::move(r->value);
        delete r;
        ready_ = true;
    }

    hce::expected<size_t, std::error_code> get_result() override {
        return result;
    }

    hce::spinlock slk_;
    detail::network::socket& sock;
    std::span<std::byte> buf;
    size_t offset = 0;
    bool ready_ = false;
    hce::expected<size_t, std::error_code> result;
};

hce::awt<hce::expected<size_t, std::error_code>>
network::connection::recv_all(std::span<std::byte> data) {
    return hce::awt<hce::expected<size_t, std::error_code>>(
        new recv_all_awaitable(socket, data));
}

// ---------------------------------------------------------------------------
// acceptor
// ---------------------------------------------------------------------------

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
            hce::awt<hce::expected<network::connection,
                                   std::error_code>>::interface>>
{
    explicit accept_awaitable(detail::network::socket& s) :
        hce::scheduler::reschedule<
            hce::awaitable::lockable<
                hce::spinlock,
                hce::awt<hce::expected<network::connection,
                                       std::error_code>>::interface>>(
                    slk_,
                    hce::awaitable::await::policy::defer,
                    hce::awaitable::resume::policy::lock),
        sock(s)
    {}

    bool on_ready() override { return ready_; }

    void on_suspend() override {
        auto& backend = hce::service<network>::get().get_backend();
        backend.register_op(
            sock.fd(),
            hce::interface::network::backend::interest::read,
            this,
            [](int, std::error_code ec, uint32_t, void* user) {
                auto* self = static_cast<accept_awaitable*>(user);
                auto* r = new connection_result;
                if(ec) {
                    r->value = std::unexpected(ec);
                } else {
                    sockaddr_storage addr{};
                    socklen_t len = sizeof(addr);
                    int cfd = ::accept4(self->sock.fd(),
                                        reinterpret_cast<sockaddr*>(&addr),
                                        &len,
                                        SOCK_NONBLOCK | SOCK_CLOEXEC);
                    if(cfd < 0) {
                        r->value = std::unexpected(std::error_code(
                            errno, std::system_category()));
                    } else {
                        network::connection conn;
                        conn.socket = detail::network::socket(cfd);
                        r->value = std::move(conn);
                    }
                }
                self->resume(r);
            });
    }

    void on_resume(void* m) override {
        auto* r = static_cast<connection_result*>(m);
        result = std::move(r->value);
        delete r;
        ready_ = true;
    }

    hce::expected<network::connection, std::error_code>
    get_result() override {
        return std::move(result);
    }

    hce::spinlock slk_;
    detail::network::socket& sock;
    bool ready_ = false;
    hce::expected<network::connection, std::error_code> result;
};

hce::awt<hce::expected<network::connection, std::error_code>>
network::acceptor::accept() {
    return hce::awt<hce::expected<network::connection, std::error_code>>(
        new accept_awaitable(socket));
}

// ---------------------------------------------------------------------------
// network service
// ---------------------------------------------------------------------------

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
            hce::awt<hce::expected<network::connection,
                                   std::error_code>>::interface>>
{
    explicit connect_awaitable(const network::address& a) :
        hce::scheduler::reschedule<
            hce::awaitable::lockable<
                hce::spinlock,
                hce::awt<hce::expected<network::connection,
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
            auto* r = new connection_result;
            r->value = std::unexpected(std::make_error_code(
                std::errc::host_unreachable));
            resume(r);
            return;
        }

        int fd = ::socket(res->ai_family,
                          res->ai_socktype | SOCK_NONBLOCK | SOCK_CLOEXEC,
                          res->ai_protocol);
        if(fd < 0) {
            ::freeaddrinfo(res);
            auto* r = new connection_result;
            r->value = std::unexpected(std::error_code(
                errno, std::system_category()));
            resume(r);
            return;
        }

        pending.socket = detail::network::socket(fd);

        int cr = ::connect(fd, res->ai_addr, res->ai_addrlen);
        ::freeaddrinfo(res);

        if(cr == 0) {
            auto* r = new connection_result;
            r->value = std::move(pending);
            resume(r);
            return;
        }
        if(errno != EINPROGRESS) {
            auto* r = new connection_result;
            r->value = std::unexpected(std::error_code(
                errno, std::system_category()));
            resume(r);
            return;
        }

        auto& backend = hce::service<network>::get().get_backend();
        backend.register_op(
            fd,
            hce::interface::network::backend::interest::write,
            this,
            [](int fd, std::error_code ec, uint32_t, void* user) {
                auto* self = static_cast<connect_awaitable*>(user);
                auto* r = new connection_result;
                if(ec) {
                    r->value = std::unexpected(ec);
                } else {
                    int err = 0;
                    socklen_t len = sizeof(err);
                    if(::getsockopt(fd, SOL_SOCKET, SO_ERROR,
                                     &err, &len) < 0 || err != 0) {
                        r->value = std::unexpected(std::error_code(
                            err ? err : errno, std::system_category()));
                    } else {
                        r->value = std::move(self->pending);
                    }
                }
                self->resume(r);
            });
    }

    void on_resume(void* m) override {
        auto* r = static_cast<connection_result*>(m);
        result = std::move(r->value);
        delete r;
        ready_ = true;
    }

    hce::expected<network::connection, std::error_code>
    get_result() override {
        return std::move(result);
    }

    hce::spinlock slk_;
    network::address addr;
    network::connection pending;
    bool ready_ = false;
    hce::expected<network::connection, std::error_code> result;
};

hce::awt<hce::expected<network::connection, std::error_code>>
network::connect(const address& addr) {
    return hce::awt<hce::expected<network::connection, std::error_code>>(
        new connect_awaitable(addr));
}

// ---- listen --------------------------------------------------------------

hce::expected<network::acceptor, std::error_code>
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
    
network::backend& network::get_backend() {
    return epoll_backend;
}

} // hce

