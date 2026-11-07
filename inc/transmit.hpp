#pragma once
//SPDX-License-Identifier: MIT
//Author: Blayne Dennis

namespace hce {
namespace transmit {

struct connection {
    virtual ~connection() = 0;

    /**
     @brief send data across the connection 

     @param data the span of bytes to send
     @return the resulting error code (-1 on error or >=0 bytes sent)
     */
    virtual hce::awt<std::error_code> send(std::span<const std::byte> data) = 0;

    /**
     @brief send all data across the connection 

     This retries till the send finishes or error.

     @param data the span of bytes to send
     @return the resulting error code (-1 on error or >=0 bytes sent)
     */
    virtual hce::awt<std::error_code> send_all(std::span<const std::byte> data) = 0;

    /**
     @brief send data across the connection 

     @param data the span of bytes to receive
     @return the resulting error code (-1 on error or >=0 bytes received)
     */
    virtual hce::awt<hce::expected<size_t, std::error_code>> recv(std::span<std::byte> data) = 0;

    /**
     @brief send all data across the connection 

     This retries till the receive finishes or error.

     @param data the span of bytes to receive
     @return the resulting error code (-1 on error or >=0 bytes received)
     */
    virtual hce::awt<hce::expected<size_t, std::error_code>> recv_all(std::span<std::byte> data) = 0;

    /// shutdown writes
    virtual void shutdown_read() = 0;

    /// shutdown reads
    virtual void shutdown_write() = 0;

    /**
     @brief close the connection
     */
    virtual void close() = 0;

    /**
     @return true if the connection is valid, else false
     */
    virtual bool valid() const = 0;
};

} // transmit
} // hce
