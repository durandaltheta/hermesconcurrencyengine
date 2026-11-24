#pragma once
//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 

#include <vector>
#include <type_traits>

#include "hce/channel/base.hpp"
#include "hce/chrono.hpp"

namespace hce {
namespace detail {
namespace select {

struct callback {
    virtual ~callback(){}
    virtual void call() = 0;
};

template <typename T> {
struct callback_send : 
    channel::detail::select::clause::send,
    callback
{
    virtual ~callback_send(){}
    inline void call() override { callback(); }
    std::unique_ptr<detail::select::callback> callback;
}

template <typename T> {
struct callback_recv : 
    channel::detail::select::clause::recv,
    callback
{
    virtual ~callback_recv(){}
    inline void call() override { callback(); }
    std::unique_ptr<detail::select::callback> callback;
}

} // select 
} // detail

/**
 @brief register with multiple channels for send or receive

 Example:
 ```
 hce::co<bool> foo(hce::channel::unbuffered<int>* ich, 
                   hce::channel::unbuffered<std::string>* sch) {
     hce::select select;
     int i;
     std::string s = "hello";
     bool done = false

     while(!done) {
         // build our channel requests
         select.recv(*ich, i, [&]{ 
             std::cout << "received int: " << i << std::endl;
         });

         select.send(*sch, s, [&]{ 
             done = true; // we can access outside context
             std::cout << "sent std::string: " << s << std::endl;
         });

         if(!select.call(co_await select.wait())) {
            // we were all canceled
            co_return false;
         }
     }
            
     co_return true;
 }
 ```
 */
struct select {
    /// register a clause in the select
    select& clause(std::unique_ptr<channel::select::clause::base> ca);

    /// register a send clause
    template <typename T, typename CALLBACK>
    select& send(channel::interface<T>& ch, T&& src, CALLBACK&& cb) {
        channel::request::op_e op;

        if constexpr (std::is_rvalue_reference_v<decltype(src)>) {
            op = channel::select::request::op_e::send_shallow;
        } else {
            op = channel::select::request::op_e::send_deep;
        }

        int idx = static_cast<int>(clauses.size());
        channel::request r{ op, &src, &group, idx };

        return clause({ 
            new callback_send<unqualified<CALLBACK>>>(
                std::forward<CALLBACK>(cb), ch, r);
        });
    }

    /// register a receive clause
    template <typename T, typename CALLBACK>
    select& recv(channel::interface<T>& ch, T& dest, CALLBACK&& cb) {
        int idx = static_cast<int>(clauses.size());

        channel::request r{ 
            channel::request::op_e::recv, 
            &dest, 
            &group, 
            idx 
        };

        return clause({ 
            new callback_recv<unqualified<CALLBACK>>>(
                std::forward<CALLBACK>(cb), ch, r);
        });
    }

    /*
     returns an awaitable that parks until exactly one case wins.

     The select object must remain alive until the awaitable completes.
     Result is the winning case index, or -1 if every case failed.

     @param timeout optional timeout duration 
     */
    hce::awt<int> wait(hce::chrono::duration timeout = hce::chrono::duration());

    /**
     @brief call the selected callback, if any 
     @param idx index of the callback returned from wait() awaitable 
     @return true on success, else false
     */
    bool call(int idx);

private:
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
