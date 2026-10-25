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

hce::interface::network::socket& network::connection::get_socket() {
    return socket;
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

    using base = hce::scheduler::reschedule<
        hce::awaitable::lockable<
            hce::spinlock,
            hce::awt<std::error_code>::interface>>;

    bool on_ready() override { return ready_; }

    void on_suspend() override {
        base::on_suspend();

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

    using base = hce::scheduler::reschedule<
        hce::awaitable::lockable<
            hce::spinlock,
            hce::awt<std::error_code>::interface>>;


    bool try_progress() {
        while(offset < data.size()) {
            std::error_code ec;
            ssize_t n = sock.send(data.subspan(offset), ec);

            if(n > 0) {
                offset += static_cast<size_t>(n);
                continue;
            }

            if(!ec || ec == std::errc::resource_unavailable_try_again) {
                return false;               // need writability
            }

            // hard error
            result = ec;
            ready_ = true;
            return true;
        }

        result = {};
        ready_ = true;
        return true;                        // all sent
    }

    bool on_ready() override {
        return ready_ || try_progress();
    }

    void on_suspend() override {
        // Capture destination scheduler (fixes the earlier null dest crash).
        hce::scheduler::reschedule<
            hce::awaitable::lockable<
                hce::spinlock,
                hce::awt<std::error_code>::interface>>::on_suspend();

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

                if(self->try_progress()) {
                    auto* r = new error_result;
                    r->ec = self->result;
                    self->resume(r);
                } else {
                    // still need more — re-register
                    self->on_suspend();
                }
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

    using base = hce::scheduler::reschedule<
        hce::awaitable::lockable<
            hce::spinlock,
            hce::awt<hce::expected<size_t, std::error_code>>::interface>>;

    bool on_ready() override { return ready_; }

    void on_suspend() override {
        base::on_suspend();

        auto& backend = hce::service<network>::get().get_backend();
        backend.register_op(
            sock.fd(),
            hce::interface::network::backend::interest::read,
            this,
            [](int, std::error_code ec, uint32_t, void* user) {
                auto* self = static_cast<recv_awaitable*>(user);
                auto* r = new size_result;
                if(ec) {
                    r->value = hce::unexpected(ec);
                } else {
                    std::error_code io_ec;
                    ssize_t n = self->sock.recv(self->buf, io_ec);
                    if(n < 0) {
                        r->value = hce::unexpected(io_ec);
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

    using base = hce::scheduler::reschedule<
        hce::awaitable::lockable<
            hce::spinlock,
            hce::awt<hce::expected<size_t, std::error_code>>::interface>>;

    bool try_progress() {
        while(offset < buf.size()) {
            std::error_code ec;
            ssize_t n = sock.recv(buf.subspan(offset), ec);

            if(n > 0) {
                offset += static_cast<size_t>(n);
                continue;
            }

            if(n == 0 && !ec) {             // EOF
                result = offset;
                ready_ = true;
                return true;
            }

            if(!ec || ec == std::errc::resource_unavailable_try_again) {
                return false;               // need readability
            }

            result = hce::unexpected(ec);
            ready_ = true;
            return true;
        }

        result = offset;
        ready_ = true;
        return true;
    }

    bool on_ready() override {
        return ready_ || try_progress();
    }

    void on_suspend() override {
        hce::scheduler::reschedule<
            hce::awaitable::lockable<
                hce::spinlock,
                hce::awt<hce::expected<size_t, std::error_code>>::interface>>::on_suspend();

        auto& backend = hce::service<network>::get().get_backend();
        backend.register_op(
            sock.fd(),
            hce::interface::network::backend::interest::read,
            this,
            [](int, std::error_code ec, uint32_t, void* user) {
                auto* self = static_cast<recv_all_awaitable*>(user);

                if(ec) {
                    auto* r = new size_result;
                    r->value = hce::unexpected(ec);
                    self->resume(r);
                    return;
                }

                if(self->try_progress()) {
                    auto* r = new size_result;
                    r->value = std::move(self->result);
                    self->resume(r);
                } else {
                    self->on_suspend();
                }
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

hce::interface::network::socket& network::acceptor::get_socket() {
    return socket;
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

    using base = hce::scheduler::reschedule<
        hce::awaitable::lockable<
            hce::spinlock,
            hce::awt<hce::expected<network::connection,
                                   std::error_code>>::interface>>;

    bool on_ready() override { return ready_; }

    void on_suspend() override {
        base::on_suspend();

        auto& backend = hce::service<network>::get().get_backend();
        backend.register_op(
            sock.fd(),
            hce::interface::network::backend::interest::read,
            this,
            [](int, std::error_code ec, uint32_t, void* user) {
                auto* self = static_cast<accept_awaitable*>(user);
                auto* r = new connection_result;
                if(ec) {
                    r->value = hce::unexpected(ec);
                } else {
                    sockaddr_storage addr{};
                    socklen_t len = sizeof(addr);
                    int cfd = ::accept4(self->sock.fd(),
                                        reinterpret_cast<sockaddr*>(&addr),
                                        &len,
                                        SOCK_NONBLOCK | SOCK_CLOEXEC);
                    if(cfd < 0) {
                        r->value = hce::unexpected(std::error_code(
                            errno, std::system_category()));
                    } else {
                        network::connection conn;
                        static_cast<detail::network::socket&>(conn.get_socket()) 
                            = detail::network::socket(cfd);
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

    using base = hce::scheduler::reschedule<
        hce::awaitable::lockable<
            hce::spinlock,
            hce::awt<hce::expected<network::connection,
                                   std::error_code>>::interface>>;

    // Returns true if the operation is already finished (success or hard error).
    bool try_connect() {
        if(ready_) return true;

        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = (addr.type == hce::network::address::type_e::udp)
            ? SOCK_DGRAM
            : SOCK_STREAM;

        addrinfo* res = nullptr;
        int rc = ::getaddrinfo(addr.host.c_str(),
                               std::to_string(addr.port).c_str(),
                               &hints, &res);
        if(rc != 0) {
            result = hce::unexpected(std::make_error_code(
                std::errc::host_unreachable));
            ready_ = true;
            return true;
        }

        int fd = ::socket(res->ai_family,
                          res->ai_socktype | SOCK_NONBLOCK | SOCK_CLOEXEC,
                          res->ai_protocol);
        if(fd < 0) {
            ::freeaddrinfo(res);
            result = hce::unexpected(std::error_code(
                errno, std::system_category()));
            ready_ = true;
            return true;
        }

        static_cast<detail::network::socket&>(pending.get_socket())
            = detail::network::socket(fd);

        int cr = ::connect(fd, res->ai_addr, res->ai_addrlen);
        ::freeaddrinfo(res);

        if(cr == 0) {
            // Immediate success (typical for UDP, possible for TCP on localhost)
            result = std::move(pending);
            ready_ = true;
            return true;
        }

        if(errno != EINPROGRESS) {
            result = hce::unexpected(std::error_code(
                errno, std::system_category()));
            ready_ = true;
            return true;
        }

        // EINPROGRESS — need writability
        return false;
    }

    bool on_ready() override {
        return try_connect();
    }

    void on_suspend() override {
        base::on_suspend();

        // try_connect() already created the socket and saw EINPROGRESS.
        // Just register for write.
        auto& backend = hce::service<network>::get().get_backend();
        int fd = static_cast<detail::network::socket&>(pending.get_socket()).fd();

        backend.register_op(
            fd,
            hce::interface::network::backend::interest::write,
            this,
            [](int fd, std::error_code ec, uint32_t, void* user) {
                auto* self = static_cast<connect_awaitable*>(user);
                auto* r = new connection_result;
                if(ec) {
                    r->value = hce::unexpected(ec);
                } else {
                    int err = 0;
                    socklen_t len = sizeof(err);
                    if(::getsockopt(fd, SOL_SOCKET, SO_ERROR,
                                     &err, &len) < 0 || err != 0) {
                        r->value = hce::unexpected(std::error_code(
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
    const bool is_udp = (addr.type == address::type_e::udp);
    int socktype = is_udp ? SOCK_DGRAM : SOCK_STREAM;

    int fd = ::socket(AF_INET, socktype | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        return hce::unexpected(std::error_code(errno, std::system_category()));
    }

    int one = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(addr.port);
    if (addr.host.empty() || addr.host == "0.0.0.0") {
        sa.sin_addr.s_addr = INADDR_ANY;
    } else if (::inet_pton(AF_INET, addr.host.c_str(), &sa.sin_addr) != 1) {
        ::close(fd);
        return hce::unexpected(std::make_error_code(std::errc::invalid_argument));
    }

    if (::bind(fd, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) < 0) {
        ::close(fd);
        return hce::unexpected(std::error_code(errno, std::system_category()));
    }

    // UDP has no listen()
    if (!is_udp && ::listen(fd, backlog) < 0) {
        ::close(fd);
        return hce::unexpected(std::error_code(errno, std::system_category()));
    }

    acceptor a;
    a.socket = detail::network::socket(fd);
    return a;
}

hce::interface::network::backend& network::get_backend() {
    return backend;
}

hce::co<hce::expected<std::vector<std::byte>, std::error_code>>
hce::network::length_prefixed::recv(hce::network::connection& conn) {
    uint32_t len_be = 0;
    auto len_span = std::as_writable_bytes(std::span(&len_be, 1));

    if(auto ec = co_await conn.recv_all(len_span)) {
        co_return hce::unexpected(ec);
    }

    const uint32_t len = ntohl(len_be);
    if(len > max_payload) {
        co_return hce::unexpected(std::make_error_code(std::errc::message_size));
    }

    std::vector<std::byte> payload(len);
    if(auto ec = co_await conn.recv_all(payload)) {
        co_return hce::unexpected(ec);
    }

    co_return payload;
}

hce::network::length_prefixed::~length_prefixed() { }

hce::co<std::error_code>
hce::network::length_prefixed::send(
        hce::network::connection& conn, 
        std::vector<std::byte> msg) 
{
    if(msg.size() > max_payload) {
        co_return std::make_error_code(std::errc::message_size);
    }

    const uint32_t len = static_cast<uint32_t>(msg.size());
    const uint32_t len_be = htonl(len);
    auto len_span = std::as_bytes(std::span(&len_be, 1));

    if(auto ec = co_await conn.send_all(len_span)) {
        co_return ec;
    }

    co_return co_await conn.send_all(msg);
}

} // hce

