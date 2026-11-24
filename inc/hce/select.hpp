#pragma once
//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 

#include <vector>
#include <type_traits>

#include "hce/channel/base.hpp"
#include "hce/chrono.hpp"

namespace hce {

/**
 @brief register with multiple channels for send or receive 

 Example:
 ```
 hce::co<bool> foo(hce::channel::unbuffered<int>* ich, 
                   hce::channel::unbuffered<std::string>* sch) {
     hce::select select;
     int i;
     std::string s = "hello";
     bool done = false;
     auto timeout = hce::chrono::milliseconds(1000);

     // build our channel requests
     select.recv(*ich, i, [&]{ 
         std::cout << "received int: " << i << std::endl;
     });

     select.send(*sch, s, [&]{ 
         done = true; 
         std::cout << "sent std::string: " << s << std::endl;
     });

     while(!done) {
         if(co_await select.wait(timeout) > -1) {
            // all clauses failed or timeout
            co_return false;
         }
     }
            
     co_return true;
 }
 ```
 */
struct select {
    /**
     @brief register a send clause and return the index 

     WARNING: callbacks are NOT coroutines.

     @param ch the channel to receive from 
     @param src the channel input to read from
     @param cb the callback to execute on success
     @param optional arguments for the callback
     */
    template <typename T, typename CALLBACK, typename... As>
    int send(channel::interface<T>& ch, T&& src, CALLBACK&& cb, As&&... as) {
        channel::request::op_e op;

        if constexpr (std::is_rvalue_reference_v<decltype(src)>) {
            op = channel::select::request::op_e::send_shallow;
        } else {
            op = channel::select::request::op_e::send_deep;
        }

        int idx = static_cast<int>(clauses.size());
        channel::request r{ op, &src, &group, idx };

        return clause({ 
            new callback_send<>>(
                package_callback(
                    std::forward<CALLBACK>(cb), 
                    std::forward<As>(as)...),
                ch, 
                r);
        });
    }

    /**
     @brief register a receive clause and return the index 

     WARNING: callbacks are NOT coroutines.

     @param ch the channel to receive from 
     @param dest the channel output to write to 
     @param cb the callback to execute on success
     @param optional arguments for the callback
     */
    template <typename T, typename CALLBACK, typename... As>
    int recv(channel::interface<T>& ch, T& dest, CALLBACK&& cb, As&&... as) {
        int idx = static_cast<int>(clauses.size());

        channel::request r{ 
            channel::request::op_e::recv, 
            &dest, 
            &group, 
            idx 
        };

        return clause({ 
            new callback_recv<unqualified<CALLBACK>>>(
                package_callback(
                    std::forward<CALLBACK>(cb), 
                    std::forward<As>(as)...),
                ch, 
                r);
        });
    }

    /// register a clause in the select
    int clause(std::unique_ptr<channel::select::clause::base> ca);

    /*
     returns an awaitable that parks until exactly one case wins.

     The select object must remain alive until the awaitable completes.
     Result is the winning case index, or -1 if every case failed.

     When this returns >-1, the associated clause won and its callback executed.

     @param timeout optional timeout duration 
     */
    hce::awt<int> wait(hce::chrono::duration timeout = hce::chrono::duration());

private:
    template <typename CALLBACK, typename A, typename... As>
    auto package_callback(CALLBACK&& cb, A&& a, As&&... as) {
        // thunkize the result 
        return [a = std::forward<A>(a), 
                ...args = std::forward<Args>(args)]() mutable -> { 
            cb(std::forward<A>(a), std::forward<As>(as)...);
        };
    }

    template <typename CALLBACK>
    auto package_callback(CALLBACK&& cb) {
        return std::forward<CALLBACK>(cb);
    }

    template <typename CALLBACK>
    struct callback_send : channel::detail::select::clause::send {
        template<typename CALLBACK_SHADOW, typename... As>
        callback_send(CALLBACK_SHADOW&& c, As&&... as) :
            channel::detail::select::clause::send(std::forward<As>(as)...),
            cb(std::forward<CALLBACK_SHADOW>(c))
        { }

        ~callback_send(){}
        inline void call() { cb(); }

        CALLBACK cb;
    };

    template <typename CALLBACK>
    struct callback_recv : channel::detail::select::clause::recv {
        template<typename CALLBACK_SHADOW, typename... As>
        callback_recv(CALLBACK_SHADOW&& c, As&&... as) :
            channel::detail::select::clause::recv(std::forward<As>(as)...),
            cb(std::forward<CALLBACK_SHADOW>(c))
        { }

        ~callback_recv(){}
        inline void call() { cb(); }

        CALLBACK cb;
    };

    channel::select::group group;
    std::vector<std::unique_ptr<clause::base>> clauses;
};

} // hce
