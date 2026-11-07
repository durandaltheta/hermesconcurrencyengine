#pragma once
//SPDX-License-Identifier: MIT
//Author: Blayne Dennis

#include <span>
#include <cstddef>
#include <string>
#include <system_error>

#include "logging.hpp"
#include "expected.hpp"
#include "coroutine.hpp"
#include "scheduler.hpp"
#include "channel.hpp"

namespace hce {

/**
 Mechanisms in this header are generically for communication with something 
 outside the running process.

 See network.hpp for the most obvious TCP/UDP implementation.
 */
namespace transmit {

struct connection : public hce::printable {
    virtual ~connection() = 0;

    static std::string info_name();
    std::string name() const;

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

/**
 @brief protocol interface for converting a raw connection to a specific data type 

 Received and sent types are optionally different.
 */
template<typename RECV_T, typename SEND_T = RECV_T>
struct protocol {
    virtual ~protocol(){}

    // Read one complete message. May call recv/recv_all internally.
    virtual hce::co<hce::expected<RECV_T, std::error_code>>
    recv(transmit::connection& conn) = 0;

    // Write one complete message. May call send/send_all internally.
    virtual hce::co<std::error_code>
    send(transmit::connection& conn, SEND_T msg) = 0;
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

     Received and sent types are optionally different.

     WARNING: proto and conn must stay in existence till state.awt is joined.

     @param proto the protocol implementation  
     @param conn the connection implementation 
     @param inbound the inbound channel
     @param outbound the outboud channel
     */
    template<typename RECV_T, typename SEND_T = RECV_T>
    static state launch(
        transmit::protocol<RECV_T,SEND_T>& proto,
        transmit::connection& conn,
        hce::chan<RECV_T> inbound,  // connection -> user
        hce::chan<SEND_T> outbound) // user -> connection
    {
        state st;
        st.awt = hce::schedule(
            pump::operation<RECV_T,SEND_T>(
                proto,
                conn,
                std::move(inbound),
                st.in,
                std::move(outbound),
                st.out));

        return st;
    }

private:
    template<typename RECV_T, typename SEND_T>
    static hce::co<void> operation(
        transmit::protocol<RECV_T,SEND_T>& proto,
        transmit::connection& conn,
        hce::chan<RECV_T> inbound,  
        hce::chan<state::result> inresult,
        hce::chan<SEND_T> outbound,
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
            SEND_T msg;
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

} // transmit
} // hce
