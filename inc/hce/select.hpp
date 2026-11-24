#pragma once
//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
// c++

#include <vector>
#include <type_traits>

#include "hce/channel/base.hpp"

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
    template <typename T, typename CALLBACK>
    select& send(channel::interface<T>& ch, T&& src, CALLBACK&& cb) {
        channel::request::op_e op;

        if constexpr (std::is_rvalue_reference_v<decltype(src)>) {
            op = channel::select::request::op_e::send_shallow;
        } else {
            op = channel::select::request::op_e::send_deep;
        }

        int idx = static_cast<int>(clauses.size());
        channel::request r{ op, &dest, group, idx };

        auto clause = new detail::select::callback_send<T>>(ch, r);
        clause->callback = callback_impl<CALLBACK>::make(std::forward<CALLBACK>(cb))
        return append(clause);
    }

    template <typename T, typename CALLBACK>
    select& recv(channel::interface<T>& ch, T& dest, CALLBACK&& cb) {
        int idx = static_cast<int>(clauses.size());

        channel::request r{ 
            channel::request::op_e::recv, 
            &dest, 
            group, 
            idx 
        };

        auto clause = new detail::select::callback_recv<T>>(ch, r);
        clause->callback = callback_impl<CALLBACK>::make(std::forward<CALLBACK>(cb))
        return append(clause);
    }

    /*
     returns an awaitable that parks until exactly one case wins.

     The select object must remain alive until the awaitable completes.
     Result is the winning case index, or -1 if every case failed.
     */
    inline hce::awt<int> wait() {
        auto state = std::make_unique<state>();
        state->group = &group;
        state->clauses = &clauses;
        return hce::awt<int>(new channel::select::awt(std::move(state)));
    }

    /**
     @brief call the selected callback 
     
     Callbacks are cleared after every call().
     */
    inline bool call(int idx) { 
        bool result = false;

        if(idx > -1 && clauses[idx]) {
            ((detail::select::callback*)(clauses[idx].get()))->call();
            result = true;
        } 

        callbacks.clear();
        return result;
    }

private:
    inline select& append_(clause::base* ca) {
        clauses.push_back(std::unique_ptr<clause::base>(ca));
        return *this;
    }

    template <typename CALLBACK>
    struct callback_impl : detail::select::callback {
        static callback_impl<CALLBACK>* make(const CALLBACK& c) { 
            return new callback_impl<CALLBACK>( c );
        }

        static callback_impl<CALLBACK>* make(CALLBACK&& c) { 
            return new callback_impl<CALLBACK>( std::move(c) );
        }

        ~callback_impl(){}
        inline void call() { cb(); }

        CALLBACK cb;
    };

    channel::select::group group;
    std::vector<std::unique_ptr<clause::base>> clauses;
};

} // hce
