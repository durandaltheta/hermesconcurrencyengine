#pragma once
//SPDX-License-Identifier: MIT
//Author: Blayne Dennis

#include <span>
#include <cstddef>

#include "base.hpp"
#include "logging.hpp"
#include "expected.hpp"
#include "network/interface.hpp"
#include "network/epoll_backend.hpp"
#include "network/socket.hpp"
#include "atomic.hpp"
#include "coroutine.hpp"
#include "scheduler.hpp"
#include "channel.hpp"

namespace hce {
namespace config {
namespace network {

/**
 @brief count of worker threads 

 If 0, count is set to hardware concurrency
*/
size_t thread_count();

}
}

/**
 @brief a tcp networking service 

 Allows coroutines to form network connections, send, and receive.
 */
struct network : public hce::service<network>, public hce::printable {
    struct address {
        enum struct type_e {
            tcp,
            udp
        };

        std::string host;
        uint16_t port = 0;
        type_e type = type_e::tcp;
    };

    struct acceptor;

    /**
     @brief a connected TCP stream. 

     Movable, owns the socket. 
     */
    struct connection {
        connection() = default;
        connection(const connection&) = delete;
        connection(connection&&) noexcept;
        connection& operator=(const connection&) = delete;
        connection& operator=(connection&&) noexcept;
        ~connection();

        /**
         @brief send data across the connection 

         @param data the span of bytes to send
         @return the resulting error code (-1 on error or >=0 bytes sent)
         */
        hce::awt<std::error_code> send(std::span<const std::byte> data);

        /**
         @brief send all data across the connection 

         This retries till the send finishes or error.

         @param data the span of bytes to send
         @return the resulting error code (-1 on error or >=0 bytes sent)
         */
        hce::awt<std::error_code> send_all(std::span<const std::byte> data);

        /**
         @brief send data across the connection 

         @param data the span of bytes to receive
         @return the resulting error code (-1 on error or >=0 bytes received)
         */
        hce::awt<hce::expected<size_t, std::error_code>> recv(std::span<std::byte> data);

        /**
         @brief send all data across the connection 

         This retries till the receive finishes or error.

         @param data the span of bytes to receive
         @return the resulting error code (-1 on error or >=0 bytes received)
         */
        hce::awt<hce::expected<size_t, std::error_code>> recv_all(std::span<std::byte> data);

        /// shutdown writes on the socket
        void shutdown_read();

        /// shutdown reads on the socket
        void shutdown_write();

        /**
         @brief close the socket connection
         */
        void close();

        /**
         @return true if the connection is valid, else false
         */
        bool valid() const;

        /// return the socket via its interface
        hce::interface::network::socket& get_socket();

    private:
        detail::network::socket socket;

        friend struct acceptor;
        friend struct network;
    };

    /**
     @brief listening server socket 
     */
    struct acceptor {
        acceptor() = default;
        acceptor(const acceptor&) = delete;
        acceptor(acceptor&&) noexcept;
        ~acceptor();

        acceptor& operator=(const acceptor&) = delete;
        acceptor& operator=(acceptor&&) noexcept;

        /**
         @brief accept one connection 

         The returned connection is already non-blocking and affinity-assigned 
         to an IO thread.

         @return an expected connection and error code
         */
        hce::awt<hce::expected<connection, std::error_code>> accept();

        /**
         @brief close the socket connection
         */
        void close();

        /// return the socket via its interface
        hce::interface::network::socket& get_socket();

    private:
        detail::network::socket socket;

        friend struct network;
    };

    /**
     @brief protocol interface for converting a raw connection to a specific data type
     */
    template<typename T>
    struct protocol {
        virtual ~protocol(){}

        // Read one complete message. May call recv/recv_all internally.
        virtual hce::co<hce::expected<T, std::error_code>>
        recv(hce::network::connection& conn) = 0;

        // Write one complete message. May call send/send_all internally.
        virtual hce::co<std::error_code>
        send(hce::network::connection& conn, T msg) = 0;
    };

    /**
     @brief length-prefixed binary framing protocol implementation 

     Length prefixing a binary blob is a common framing protocol pattern.

     It is included here as an optional helper.

     Message layout: uint32_t length (network order) followed by that many payload bytes.
     T is std::vector<std::byte>.
     */
    struct length_prefixed : public protocol<std::vector<std::byte>> {
        static constexpr size_t max_payload = 16 * 1024 * 1024; // arbitrary safety limit

        ~length_prefixed();

        hce::co<hce::expected<std::vector<std::byte>, std::error_code>>
        recv(hce::network::connection& conn);

        hce::co<std::error_code>
        send(hce::network::connection& conn, std::vector<std::byte> msg);
    };

    /**
     @brief pump data between a connection and inbound/outbound channels over some protocol
     */
    template<typename T>
    static hce::co<void> pump(
        hce::network::protocol<T>& proto,
        hce::network::connection conn,
        hce::chan<T> inbound,  // connection -> user
        hce::chan<T> outbound) // user -> connection
    {
        // Two concurrent directions.
        auto reader = [&]() -> hce::co<void> {
            while(conn.valid()) {
                auto msg = co_await hce::schedule(proto.recv(conn));

                if(!msg) { 
                    break; // error or clean EOF
                }

                if(!co_await inbound.send(std::move(msg.value()))) {
                    break; // user closed the channel 
                }
            }

            conn.shutdown_read();
            inbound.close();
            co_return;
        };

        auto writer = [&]() -> hce::co<void> {
            T msg;

            while(co_await outbound.recv(msg)) {
                if(auto ec = co_await hce::schedule(proto.send(conn, std::move(msg)))) {
                    break;
                }
            }

            conn.shutdown_write();
            outbound.close();
            co_return;
        };

        auto reader_awt = hce::schedule(reader());
        auto writer_awt = hce::schedule(writer());
        co_await std::move(reader_awt);
        co_await std::move(writer_awt);
        conn.close();
    }

    network(const network&) = delete;
    network(network&&) = delete;
    virtual ~network();

    static std::string info_name();
    std::string name() const;

    network& operator=(const network&) = delete;
    network& operator=(network&&) = delete;

    /**
     @brief establish a client connection 

     @param addr the address to connect to
     @return an expected connection and error code
     */
    hce::awt<hce::expected<connection, std::error_code>> connect(const address& addr);

    /**
     @brief establish a server connection

     @param addr the address to connect to
     @param backlog the maximum length to which the queue of pending connections for sockfd may grow.
     @return an expected acceptor and error code
     */
    hce::expected<acceptor, std::error_code> listen(const address& addr, int backlog = SOMAXCONN);

    /// return the backend via its interface
    hce::interface::network::backend& get_backend();

private:
    network();

    hce::detail::network::epoll_backend backend;

    friend hce::lifecycle;
};

}
