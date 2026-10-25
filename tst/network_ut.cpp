//SPDX-License-Identifier: MIT
//Author: Blayne Dennis

#include <gtest/gtest.h>

#include "network.hpp"
#include "scheduler.hpp"
#include "coroutine.hpp"
#include "service.hpp"
#include "test_helpers.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cstring>
#include <span>
#include <string>
#include <vector>

namespace test::network {

/// Retrieve the locally-bound port of an acceptor (useful when listening on port 0).
uint16_t get_local_port(hce::network::acceptor& acc) {
    sockaddr_in sa{};
    socklen_t len = sizeof(sa);
    if(::getsockname(acc.get_socket().fd(),
                     reinterpret_cast<sockaddr*>(&sa),
                     &len) < 0) {
        return 0;
    }
    return ntohs(sa.sin_port);
}

/// Simple echo server coroutine: accept one connection, recv_all of the
/// expected size, echo it back with send_all, close.
hce::co<void> echo_once(hce::network::acceptor acc,
                        size_t expected,
                        test::queue<std::string>& status) {
    auto conn_exp = co_await acc.accept();
    if(!conn_exp) {
        status.push("accept failed: " + conn_exp.error().message());
        co_return;
    }

    auto conn = std::move(conn_exp.value());
    if(!conn.valid()) {
        status.push("connection not valid after accept");
        co_return;
    }

    std::vector<std::byte> buf(expected);
    auto recv_exp = co_await conn.recv_all(std::span(buf));
    if(!recv_exp) {
        status.push("recv_all failed: " + recv_exp.error().message());
        co_return;
    }
    if(recv_exp.value() != expected) {
        status.push("server size mismatch");
        co_return;
    }

    auto send_ec = co_await conn.send_all(std::span(buf));
    if(send_ec) {
        status.push("send_all failed: " + send_ec.message());
        co_return;
    }

    conn.close();
    status.push("ok");
    co_return;
}

/// Client coroutine: connect, send a string, recv echo, verify, close.
hce::co<void> client_echo(hce::network::address addr,
                          std::string payload,
                          test::queue<std::string>& status) {
    auto& net = hce::service<hce::network>::get();
    auto conn_exp = co_await net.connect(addr);
    if(!conn_exp) {
        status.push("connect failed: " + conn_exp.error().message());
        co_return;
    }

    auto conn = std::move(conn_exp.value());
    if(!conn.valid()) {
        status.push("connection not valid after connect");
        co_return;
    }

    std::vector<std::byte> send_buf(payload.size());
    std::memcpy(send_buf.data(), payload.data(), payload.size());

    auto send_ec = co_await conn.send_all(std::span(send_buf));
    if(send_ec) {
        status.push("send_all failed: " + send_ec.message());
        co_return;
    }

    std::vector<std::byte> recv_buf(payload.size());
    auto recv_exp = co_await conn.recv_all(std::span(recv_buf));
    if(!recv_exp) {
        status.push("recv_all failed: " + recv_exp.error().message());
        co_return;
    }

    if(recv_exp.value() != payload.size()) {
        status.push("size mismatch");
        co_return;
    }

    std::string got(reinterpret_cast<const char*>(recv_buf.data()),
                    recv_exp.value());
    if(got != payload) {
        status.push("data mismatch");
        co_return;
    }

    conn.close();
    status.push("ok");
    co_return;
}

/// Server that accepts several connections and echoes each full payload.
hce::co<void> multi_echo(hce::network::acceptor acc,
                         size_t count,
                         size_t expected,
                         test::queue<std::string>& status) {
    for(size_t i = 0; i < count; ++i) {
        auto conn_exp = co_await acc.accept();
        if(!conn_exp) {
            status.push("accept failed");
            co_return;
        }
        auto conn = std::move(conn_exp.value());

        std::vector<std::byte> buf(expected);
        auto recv_exp = co_await conn.recv_all(std::span(buf));
        if(!recv_exp) {
            status.push("recv_all failed");
            co_return;
        }
        if(recv_exp.value() != expected) {
            status.push("server size mismatch");
            co_return;
        }
        auto send_ec = co_await conn.send_all(std::span(buf));
        if(send_ec) {
            status.push("send_all failed");
            co_return;
        }
        conn.close();
    }
    status.push("ok");
    co_return;
}

} // namespace test::network

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST(network, address_construction) {
    hce::network::address a;
    EXPECT_TRUE(a.host.empty());
    EXPECT_EQ(a.port, 0);
    EXPECT_EQ(a.type, hce::network::address::type_e::tcp);

    hce::network::address b{"127.0.0.1", 8080, hce::network::address::type_e::udp};
    EXPECT_EQ(b.host, "127.0.0.1");
    EXPECT_EQ(b.port, 8080);
    EXPECT_EQ(b.type, hce::network::address::type_e::udp);
}

TEST(network, listen_ephemeral_port) {
    auto& net = hce::service<hce::network>::get();

    auto acc_exp = net.listen(
        {"127.0.0.1", 0, hce::network::address::type_e::tcp});
    ASSERT_TRUE(acc_exp) << acc_exp.error().message();

    auto acc = std::move(acc_exp.value());
    EXPECT_TRUE(acc.get_socket().valid());

    uint16_t port = test::network::get_local_port(acc);
    EXPECT_GT(port, 0);

    acc.close();
    EXPECT_FALSE(acc.get_socket().valid());
}

TEST(network, listen_any_interface) {
    auto& net = hce::service<hce::network>::get();

    auto acc_exp = net.listen(
        {"0.0.0.0", 0, hce::network::address::type_e::tcp});
    ASSERT_TRUE(acc_exp);

    auto acc = std::move(acc_exp.value());
    EXPECT_GT(test::network::get_local_port(acc), 0);
    acc.close();
}

TEST(network, listen_invalid_host) {
    auto& net = hce::service<hce::network>::get();

    auto acc_exp = net.listen(
        {"not-a-valid-ip", 0, hce::network::address::type_e::tcp});
    EXPECT_FALSE(acc_exp);
}

TEST(network, basic_echo_localhost) {
    auto& net = hce::service<hce::network>::get();

    auto acc_exp = net.listen(
        {"127.0.0.1", 0, hce::network::address::type_e::tcp});
    ASSERT_TRUE(acc_exp);

    auto acc = std::move(acc_exp.value());
    uint16_t port = test::network::get_local_port(acc);
    ASSERT_GT(port, 0);

    test::queue<std::string> server_status;
    test::queue<std::string> client_status;

    const std::string payload = "hello hermes";

    // Schedule server and client on the global scheduler
    auto server_awt = hce::schedule(
        test::network::echo_once(std::move(acc), payload.size(), server_status));
    auto client_awt = hce::schedule(
        test::network::client_echo(
            {"127.0.0.1", port, hce::network::address::type_e::tcp},
            payload,
            client_status));

    // Wait for both to finish (blocking conversion of awt, or just drain queues)
    // The queues act as the synchronization barrier.
    EXPECT_EQ(server_status.pop(), "ok");
    EXPECT_EQ(client_status.pop(), "ok");

    // Ensure the awaitables are consumed (optional, but clean)
    (void)server_awt;
    (void)client_awt;
}

TEST(network, send_recv_modest_payload) {
    auto& net = hce::service<hce::network>::get();

    auto acc_exp = net.listen(
        {"127.0.0.1", 0, hce::network::address::type_e::tcp});
    ASSERT_TRUE(acc_exp);
    auto acc = std::move(acc_exp.value());
    uint16_t port = test::network::get_local_port(acc);
    ASSERT_GT(port, 0);

    test::queue<std::string> server_status;
    test::queue<std::string> client_status;

    std::string payload(128, 'x');
    for(size_t i = 0; i < payload.size(); ++i) {
        payload[i] = static_cast<char>('a' + (i % 26));
    }

    auto server_awt = hce::schedule(
        test::network::echo_once(std::move(acc), payload.size(), server_status));
    auto client_awt = hce::schedule(
        test::network::client_echo(
            {"127.0.0.1", port, hce::network::address::type_e::tcp},
            payload,
            client_status));

    EXPECT_EQ(server_status.pop(), "ok");
    EXPECT_EQ(client_status.pop(), "ok");

    (void)server_awt;
    (void)client_awt;
}

TEST(network, multiple_sequential_clients) {
    constexpr size_t N = 4;
    auto& net = hce::service<hce::network>::get();

    auto acc_exp = net.listen(
        {"127.0.0.1", 0, hce::network::address::type_e::tcp});
    ASSERT_TRUE(acc_exp);
    auto acc = std::move(acc_exp.value());
    uint16_t port = test::network::get_local_port(acc);
    ASSERT_GT(port, 0);

    test::queue<std::string> server_status;
    std::vector<test::queue<std::string>> client_statuses(N);

    constexpr size_t payload_size = 16;
    auto server_awt = hce::schedule(
        test::network::multi_echo(std::move(acc), N, payload_size, server_status));

    std::vector<hce::awt<void>> client_awts;
    for(size_t i = 0; i < N; ++i) {
        std::string payload(payload_size, static_cast<char>('A' + i));
        client_awts.push_back(hce::schedule(
            test::network::client_echo(
                {"127.0.0.1", port, hce::network::address::type_e::tcp},
                payload,
                client_statuses[i])));
    }

    EXPECT_EQ(server_status.pop(), "ok");
    for(size_t i = 0; i < N; ++i) {
        EXPECT_EQ(client_statuses[i].pop(), "ok");
    }

    (void)server_awt;
    for(auto& a : client_awts) { (void)a; }
}

TEST(network, connect_refused) {
    auto& net = hce::service<hce::network>::get();

    // Port 1 is almost certainly not listening
    hce::network::address addr{
        "127.0.0.1", 1, hce::network::address::type_e::tcp};

    test::queue<std::string> status;
    auto awt = hce::schedule([&]() -> hce::co<void> {
        auto conn_exp = co_await net.connect(addr);
        if(conn_exp) {
            status.push("unexpected success");
        } else {
            status.push("failed as expected: " + conn_exp.error().message());
        }
        co_return;
    }());

    auto msg = status.pop();
    EXPECT_TRUE(msg.find("failed as expected") != std::string::npos);
    (void)awt;
}

TEST(network, close_acceptor) {
    auto& net = hce::service<hce::network>::get();

    auto acc_exp = net.listen(
        {"127.0.0.1", 0, hce::network::address::type_e::tcp});
    ASSERT_TRUE(acc_exp);
    auto acc = std::move(acc_exp.value());
    EXPECT_TRUE(acc.get_socket().valid());

    acc.close();
    EXPECT_FALSE(acc.get_socket().valid());
    // Note: accept() on a closed acceptor is not currently required to
    // fail promptly; the backend simply never delivers an event for an
    // invalid fd. Do not co_await accept() after close.
}

TEST(network, connection_valid_after_move) {
    auto& net = hce::service<hce::network>::get();

    auto acc_exp = net.listen(
        {"127.0.0.1", 0, hce::network::address::type_e::tcp});
    ASSERT_TRUE(acc_exp);
    auto acc = std::move(acc_exp.value());
    uint16_t port = test::network::get_local_port(acc);

    test::queue<std::string> status;

    // Move the acceptor into the coroutine so its lifetime is tied to the
    // coroutine frame (same pattern as the other tests that pass).
    auto server_awt = hce::schedule(
        [](hce::network::acceptor acc, test::queue<std::string>& st) -> hce::co<void> {
            auto conn_exp = co_await acc.accept();
            if(!conn_exp) {
                st.push("accept fail");
                co_return;
            }
            auto conn = std::move(conn_exp.value());
            hce::network::connection moved = std::move(conn);
            if(!moved.valid()) {
                st.push("moved connection not valid");
                co_return;
            }
            if(conn.valid()) {
                st.push("moved-from connection still valid");
                co_return;
            }
            moved.close();
            st.push("ok");
            co_return;
        }(std::move(acc), status));

    auto client_awt = hce::schedule(
        [](hce::network::address addr, test::queue<std::string>& st) -> hce::co<void> {
            auto& n = hce::service<hce::network>::get();
            auto conn_exp = co_await n.connect(addr);
            if(!conn_exp) {
                st.push("connect fail");
                co_return;
            }
            auto conn = std::move(conn_exp.value());
            conn.close();
            co_return;
        }(hce::network::address{"127.0.0.1", port,
                               hce::network::address::type_e::tcp},
          status));

    EXPECT_EQ(status.pop(), "ok");
    (void)server_awt;
    (void)client_awt;
}

// ---------------------------------------------------------------------------
// UDP helpers (connected datagram style)
// ---------------------------------------------------------------------------

namespace test::network {

/// Bound UDP socket that receives one datagram and echoes it back.
/// Requires the peer to have connected first (so send/recv suffice).
hce::co<void> udp_echo_once(hce::network::acceptor acc,
                            size_t expected,
                            test::queue<std::string>& status) {
    // For connected UDP the "acceptor" is just the bound socket.
    // We do not call accept(); we use the socket directly via a temporary
    // connection wrapper if the API allows, or via the backend.
    // Minimal version: create a connection that owns the same fd
    // (implementation detail — see note below).

    // ... practical form once you expose a way to obtain a connection
    // from a bound UDP acceptor, or add send/recv directly on acceptor
    // for datagram mode ...

    status.push("udp echo not yet wired — needs connection-from-acceptor or recvfrom");
    co_return;
}

} // namespace test::network

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST(network, udp_listen_ephemeral_port) {
    auto& net = hce::service<hce::network>::get();

    auto acc_exp = net.listen(
        {"127.0.0.1", 0, hce::network::address::type_e::udp});
    ASSERT_TRUE(acc_exp) << acc_exp.error().message();

    auto acc = std::move(acc_exp.value());
    EXPECT_TRUE(acc.get_socket().valid());

    uint16_t port = test::network::get_local_port(acc);
    EXPECT_GT(port, 0);

    acc.close();
    EXPECT_FALSE(acc.get_socket().valid());
}

TEST(network, udp_listen_any_interface) {
    auto& net = hce::service<hce::network>::get();

    auto acc_exp = net.listen(
        {"0.0.0.0", 0, hce::network::address::type_e::udp});
    ASSERT_TRUE(acc_exp);

    auto acc = std::move(acc_exp.value());
    EXPECT_GT(test::network::get_local_port(acc), 0);
    acc.close();
}

TEST(network, udp_connect_and_send_recv) {
    auto& net = hce::service<hce::network>::get();

    // Server binds
    auto acc_exp = net.listen(
        {"127.0.0.1", 0, hce::network::address::type_e::udp});
    ASSERT_TRUE(acc_exp);
    auto acc = std::move(acc_exp.value());
    uint16_t port = test::network::get_local_port(acc);
    ASSERT_GT(port, 0);

    const std::string payload = "udp hello";

    test::queue<std::string> client_status;

    // Client connects (sets default peer) and does a round-trip
    // once the server side can also send/recv on the bound socket.
    auto client_awt = hce::schedule(
        [&]() -> hce::co<void> {
            auto conn_exp = co_await net.connect(
                {"127.0.0.1", port, hce::network::address::type_e::udp});
            if (!conn_exp) {
                client_status.push("connect failed: " + conn_exp.error().message());
                co_return;
            }
            auto conn = std::move(conn_exp.value());

            std::vector<std::byte> buf(payload.size());
            std::memcpy(buf.data(), payload.data(), payload.size());

            auto send_ec = co_await conn.send_all(std::span(buf));
            if (send_ec) {
                client_status.push("send failed: " + send_ec.message());
                co_return;
            }

            // For a true echo the server must reply; this is the client half.
            client_status.push("sent");
            conn.close();
            co_return;
        }());

    EXPECT_EQ(client_status.pop(), "sent");
    (void)client_awt;
    acc.close();
}

TEST(network, udp_address_type_preserved) {
    hce::network::address a{"127.0.0.1", 9000, hce::network::address::type_e::udp};
    EXPECT_EQ(a.type, hce::network::address::type_e::udp);
    EXPECT_EQ(a.port, 9000);
}
