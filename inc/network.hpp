#pragma once
//SPDX-License-Identifier: MIT
//Author: Blayne Dennis
#include <expected>
#include <span>
#include <byte>

#include <netinet/in.h>
#include <sys/socket.h>

#include "base.hpp"
#include "logging.hpp"
#include "network/epoll_backend.hpp"
#include "network/socket.hpp"
#include "atomic.hpp"
#include "coroutine.hpp"
#include "scheduler.hpp"

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
        type_e type;
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
        hce::awt<std::expected<size_t, std::error_code>> recv(std::span<std::byte> data);

        /**
         @brief send all data across the connection 

         This retries till the receive finishes or error.

         @param data the span of bytes to receive
         @return the resulting error code (-1 on error or >=0 bytes received)
         */
        hce::awt<std::expected<size_t, std::error_code>> recv_all(std::span<std::byte> data);

        /**
         @brief close the socket connection
         */
        void close();

        /**
         @return true if the connection is valid, else false
         */
        bool valid() const;

        /**
         @return the underlying file descriptor
         */
        int fd() const;

    private:
        detail::network::socket socket;

        friend class acceptor;
        friend struct network;
    };

    /**
     @brief listening server socket 
     */
    struct acceptor {
        acceptor() = default;
        acceptor(const acceptor&) delete;
        acceptor(acceptor&&) noexcept;
        ~acceptor();

        acceptor& operator=(const acceptor&) delete;
        acceptor& operator=(acceptor&&) noexcept;

        /**
         @brief accept one connection 

         The returned connection is already non-blocking and affinity-assigned 
         to an IO thread.

         @return an expected connection and error code
         */
        hce::awt<std::expected<connection, std::error_code>> accept();

        /**
         @brief close the socket connection
         */
        void close();

        /**
         @return the underlying file descriptor
         */
        int fd() const;

    private:
        detail::network::socket socket;

        friend struct network;
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
    hce::awt<std::expected<connection, std::error_code>> connect(const address& addr);

    /**
     @brief establish a server connection

     @param addr the address to connect to
     @param backlog the maximum length to which the queue of pending connections for sockfd may grow.
     @return an expected acceptor and error code
     */
    std::expected<acceptor, std::error_code> listen(const address& addr, int backlog = SOMAXCONN);

private:
    network();

    detail::network::epoll_backend backend;
};

}
