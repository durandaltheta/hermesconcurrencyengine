#pragma once
//SPDX-License-Identifier: MIT
//Author: Blayne Dennis

#include "network.hpp"
#include "coroutine.hpp"
#include "scheduler.hpp"
#include "channel.hpp"

namespace hce {

/**
 @brief protocol interface for managing a raw connection sending and receiving bytes
 */
template<typename T>
struct protocol {
    // Read one complete message. May call recv/recv_all internally.
    static hce::co<hce::expected<T, std::error_code>>
    recv(hce::network::connection& conn);

    // Write one complete message. May call send/send_all internally.
    static hce::co<std::error_code>
    send(hce::network::connection& conn, T msg);
};

/**
 @brief bridge a connection to inbound/outbound channels over some PROTOCOL

 PROTOCOL should be some implementation of hce::protocol<T>.
 */
template<typename T, typename PROTOCOL>
hce::co<void> bridge(
    hce::network::connection conn,
    hce::chan<T> inbound,   // connection → user
    hce::chan<T> outbound)  // user → connection
{
    // Two concurrent directions.
    auto reader = [&]() -> hce::co<void> {
        while(conn.valid()) {
            auto msg = co_await PROTOCOL::recv(conn);

            if(!msg) { 
                break; // error or clean EOF
            }

            if(!co_await inbound.send(std::move(msg.value()))) {
                break; // user closed the channel 
            }
        }

        inbound.close();
    };

    auto writer = [&]() -> hce::co<void> {
        T msg;

        while(co_await outbound.recv(msg)) {
            if(auto ec = co_await PROTOCOL::send(conn, std::move(msg))) {
                break;
            }
        }

        outbound.close();
    };

    auto reader_awt = hce::schedule(reader);
    auto writer_awt = hce::schedule(writer);
    co_await std::move(reader_awt);
    co_await std::move(writer_awt);
    conn.close();
}

} // hce
