#pragma once
//SPDX-License-Identifier: MIT
//Author: Blayne Dennis

#include <arpa/inet.h>  // ntohl / htonl

#include "base.hpp"
#include "logging.hpp"
#include "expected.hpp"
#include "transmit.hpp"
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
    struct connection : public hce::transmit::connection {
        connection() = default;
        connection(const connection&) = delete;
        connection(connection&&) noexcept;
        connection& operator=(const connection&) = delete;
        connection& operator=(connection&&) noexcept;
        ~connection();

        static std::string info_name();
        std::string name() const;

        hce::awt<std::error_code> send(std::span<const std::byte> data) override;
        hce::awt<std::error_code> send_all(std::span<const std::byte> data) override;
        hce::awt<hce::expected<size_t, std::error_code>> recv(std::span<std::byte> data) override;
        hce::awt<hce::expected<size_t, std::error_code>> recv_all(std::span<std::byte> data) override;
        void shutdown_read() override;
        void shutdown_write() override;
        void close() override;
        bool valid() const override;

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
     @brief library defined network protocols 

     These are limited in scope and purely for user convenience. Expect to write 
     additional protocols when interacting with outside APIs.
     */
    struct protocol {
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
            struct byte : public transmit::protocol<std::vector<std::byte>> {
                static constexpr size_t max_payload = 16 * 1024 * 1024; ///< arbitrary safety limit

                ~byte();

                /// expects an hce::network::connection
                hce::co<hce::expected<std::vector<std::byte>, std::error_code>>
                recv(hce::transmit::connection& conn) override;

                /// expects an hce::network::connection
                hce::co<std::error_code>
                send(hce::transmit::connection& conn, std::vector<std::byte> msg) override;
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
            struct base64 : public transmit::protocol<std::basic_string<CharT,Traits,Allocator>> {
                using STRING = std::basic_string<CharT,Traits,Allocator>;

                static constexpr size_t max_payload = 16 * 1024 * 1024; ///< arbitrary safety limit

                hce::co<hce::expected<
                    std::basic_string<CharT, Traits, Allocator>,
                    std::error_code> 
                >
                recv(hce::transmit::connection& conn) override {
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

                hce::co<std::error_code> send(hce::transmit::connection& conn, STRING msg) override {
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
        }; // length_prefixed
    }; // protocol 

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
