#pragma once
//SPDX-License-Identifier: MIT
//Author: Blayne Dennis

#include <span>
#include <string>
#include <cstddef>
#include <optional>

#include <arpa/inet.h>  // ntohl / htonl

#include "base.hpp"
#include "logging.hpp"
#include "expected.hpp"
#include "codec/base64.hpp"
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
     @brief length-prefixed protocol implementations 

     Length prefixing is a common pattern in communication. Two programs written 
     with this library can make use of the protocols in this section to 
     implement communication.
     */
    struct length_prefixed {
        /**
         @brief length-prefixed binary framing protocol implementation 

         Length prefixing a binary blob is a common framing protocol pattern. 

         This protocol can be combined with a binary serialization framework 
         like flatbuffers or protobuffers to data in a highly efficient 
         manner.

         It is included here as an optional helper.

         Message layout: uint32_t length (network order) followed by that many 
         payload bytes.
         */
        struct byte : public protocol<std::vector<std::byte>> {
            static constexpr size_t max_payload = 16 * 1024 * 1024; ///< arbitrary safety limit

            ~byte();

            hce::co<hce::expected<std::vector<std::byte>, std::error_code>>
            recv(hce::network::connection& conn) override;

            hce::co<std::error_code>
            send(hce::network::connection& conn, std::vector<std::byte> msg) override;
        };

        /**
         @brief length-prefixed base64 string framing protocol implementation 

         This protocol converts length framed base64 strings.

         This protocol allows convenient sending and receiving of data, as 
         long as that data can be converted to and from some intermediate 
         language like JSON or YAML. This is less efficient than pure binary 
         but is simple to implement for all kinds of APIs.

         The input and output is compatible with std::string (an instantiation 
         of std::basic_string).

         Message layout: uint32_t encoded base64 string length (network order) 
         followed by that many payload bytes.
         */
        template<
            class CharT = char,  
            class Traits = std::char_traits<CharT>,
            class Allocator = std::allocator<CharT>
        >
        struct base64 : public protocol<std::basic_string<CharT,Traits,Allocator>> {
            using STRING = std::basic_string<CharT,Traits,Allocator>;

            static constexpr size_t max_payload = 16 * 1024 * 1024; ///< arbitrary safety limit

            hce::co<hce::expected<
                std::basic_string<CharT, Traits, Allocator>,
                std::error_code> 
            >
            recv(connection& conn) override {
                uint32_t len_be = 0;
                auto len_span = std::as_writable_bytes(std::span(&len_be, 1));
                auto len_exp = co_await conn.recv_all(len_span);

                if(!len_exp || len_exp.value() != sizeof(uint32_t)) {
                    co_return hce::unexpected(
                        len_exp ? std::make_error_code(std::errc::connection_reset)
                                : len_exp.error());
                }

                const uint32_t len = ntohl(len_be);
                if(len > max_payload) {
                    co_return hce::unexpected(std::make_error_code(std::errc::message_size));
                }

                std::string b64(len, '\0');
                auto b64_span = std::as_writable_bytes(std::span(b64.data(), b64.size()));
                auto payload_exp = co_await conn.recv_all(b64_span);

                if(!payload_exp || payload_exp.value() != len) {
                    co_return hce::unexpected(
                        payload_exp ? std::make_error_code(std::errc::connection_reset)
                                    : payload_exp.error());
                }

                auto decoded = hce::base64::decode_to<CharT, Traits, Allocator>(b64);
                if(!decoded) {
                    co_return hce::unexpected(decoded.error());
                }

                co_return std::move(decoded.value());
            }

            hce::co<std::error_code> send(connection& conn, STRING msg) override {
                auto b64 = hce::base64::encode(msg);
                if(b64.size() > max_payload) {
                    co_return std::make_error_code(std::errc::message_size);
                }

                const uint32_t len = static_cast<uint32_t>(b64.size());
                const uint32_t len_be = htonl(len);
                auto len_span = std::as_bytes(std::span(&len_be, 1));

                if(auto ec = co_await conn.send_all(len_span)) {
                    co_return ec;
                }

                auto b64_span = std::as_bytes(std::span(b64.data(), b64.size()));
                co_return co_await conn.send_all(b64_span);
            }
        };
    };

    /**
     @brief abstracts data transfer over a connection using a protocol and handling of errors
     */
    struct pump {
        /**
         @brief track the state of the pump operations
         */
        struct state {
            /**
             @brief the resulting state of some pump operation
             
             If has_value() == true, contains the error_code the connection 
             ended on.

             An error_code does not necessarily mean an error occurred, only 
             that side of the pump operation ended due to the connection closed.

             If has_value() == false, then that side of the pump operation ended 
             due to that channel being closed early.
             */
            using result = std::optional<std::error_code>;

            state();
            state(const state&) = delete;
            state(state&&) = default;

            /// closes in & out channels
            ~state();

            state& operator=(const state&) = delete;
            state& operator=(state&&) = default;

            /// The awaitable to join with the pump coroutine
            hce::awt<void> awt; 

            /// buffered channel containing the state of the input pump operation
            hce::chan<result> in; 

            /// buffered channel containing the state of the output pump operation
            hce::chan<result> out;
        };

        /**
         @brief launch operations to pump data between a connection and inbound/outbound channels over some protocol 
         */
        template<typename T>
        static state launch(
            hce::network::protocol<T>& proto,
            hce::network::connection conn,
            hce::chan<T> inbound,  // connection -> user
            hce::chan<T> outbound) // user -> connection
        {
            state st;
            st.awt = hce::schedule(
                pump::operation<T>(
                    proto,
                    std::move(conn),
                    std::move(inbound),
                    st.in,
                    std::move(outbound),
                    st.out));

            return st;
        }

    private:
        template<typename T>
        static hce::co<void> operation(
            hce::network::protocol<T>& proto,
            hce::network::connection conn,
            hce::chan<T> inbound,  
            hce::chan<state::result> inresult,
            hce::chan<T> outbound,
            hce::chan<state::result> outresult)
        {
            // Two concurrent directions.
            auto reader = [&]() -> hce::co<void> {
                state::result res;

                while(conn.valid()) {
                    auto msg = co_await hce::schedule(proto.recv(conn));

                    if(!msg) { 
                        res = msg.error();
                        co_await inresult.send(res);
                        break; // error or clean EOF
                    }

                    if(!co_await inbound.send(std::move(msg.value()))) {
                        break; // user closed the channel 
                    }
                }

                if(!res) {
                    co_await inresult.send(res);
                }

                conn.shutdown_read();
                inbound.close();
                co_return;
            };

            auto writer = [&]() -> hce::co<void> {
                T msg;
                state::result res;

                while(co_await outbound.recv(msg)) {
                    if(auto ec = co_await hce::schedule(proto.send(conn, std::move(msg)))) {
                        res = ec;
                        co_await outresult.send(res);
                        break;
                    }
                }

                if(!res) {
                    co_await outresult.send(res);
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
            co_return;
        }
    };

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
