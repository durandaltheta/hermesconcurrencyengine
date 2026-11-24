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
 hce::co<bool> foo(hce::channel::unbuffered<int>& ich, 
                   hce::channel::unbuffered<std::string>& sch) {
     hce::select select;
     int i;
     std::string s = "hello";
     bool done = false;
     auto timeout = hce::chrono::milliseconds(1000);

     while(!done) {
         // build our clauses each time
         select.recv(ich, i); // gets index 0
         select.send(sch, s); // gets index 1
         auto result = co_await select.wait(timeout);

         switch(result.index) {
             case 0:
                 std::cout << "received int: " << i << std::endl;
             case 1:
                 done = true; 
                 std::cout << "sent std::string: " << s << std::endl;
             default:
                 std::cout << "an error occurred: " << result.index << std::endl;
                 co_return false;
         }
     }
            
     co_return true;
 }
 ```

 Because the above example uses a switch statement, each case remains as part 
 of the coroutine, and allows the user to `co_await` further operations.

 In trivial cases a small switch statement is easy to read. However, select 
 send()/recv() methods allow for callbacks to be assigned. This is useful for 
 complex cases or if the user can't easily manage indexes.

 WARNING: callbacks are not coroutines!

 Example:
 ```
 hce::co<bool> foo(hce::channel::unbuffered<int>* ich, 
                   hce::channel::unbuffered<std::string>* sch) {
     hce::select select;
     int i;
     std::string s = "hello";
     bool done = false;
     auto timeout = hce::chrono::milliseconds(1000);

     while(!done) {
         select.recv(ich, i, [&]{ 
             std::cout << "received int: " << i << std::endl;
         });

         select.send(sch, s, [&]{ 
             done = true; 
             std::cout << "sent std::string: " << s << std::endl;
         });

         if(!(auto result = co_await select.wait(timeout))) {
            std::cout << "an error occurred: " << result.index << std::endl;
            co_return false;
         }
     }
            
     co_return true;
 }
 ```
 */
struct select {
    /**
     @brief object for interpretting results of a wait()

     Example:
     ```
     hce::select select;

     //... do select operations

     if(hce::select::result result = co_await select.wait()) {
        // success 
     } else {
        // handle result.error()
     }
     ```
     */
    struct result {
        enum struct error_e : int {
            timeout = -3,          ///< timeout occurred 
            user_cancelled = -2    ///< user called select::cancel()
            channel_cancelled = -1,///< underlying channels were cancelled
            ok = 0,
        };

        /// return true if ok, else false
        bool operator bool();

        /// get the specific status
        error_e status();

        /// underlying index value
        int index=-1;
    };

    /// return the current count of clauses
    int size();

    /**
     @brief register a send clause and return the index 

     WARNING: callbacks are NOT coroutines.

     @param ch the channel to receive from 
     @param src the channel input to read from
     @param as... an optional callback and its arguments to execute on success
     @return the index of the clause
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
            new make_send(ch, r,std::forward<As>(as)...);
        });
    }

    /**
     @brief register a receive clause and return the index 

     WARNING: callbacks are NOT coroutines.

     @param ch the channel to receive from 
     @param dest the channel output to write to 
     @param as... an optional callback and its arguments to execute on success
     @return the index of the clause
     */
    template <typename T, typename... As>
    int recv(channel::interface<T>& ch, T& dest, As&&... as) {
        int idx = static_cast<int>(clauses.size());

        channel::request r{ 
            channel::request::op_e::recv, 
            &dest, 
            &group, 
            idx 
        };

        return clause({ 
            new make_recv(ch, r, std::forward<As>(as)...);
        });
    }

    /**
     @brief register some clause with the select
     */
    int clause(std::unique_ptr<channel::select::clause::base> cb);

    /**
     returns an awaitable that parks until exactly one case wins.

     The select object must remain alive until the awaitable completes.
     Result is the winning case index, or -1 if every case failed.

     When this returns >-1, the associated clause won and its callback executed.

     @param timeout optional timeout duration 
     */
    hce::awt<result> wait(hce::chrono::duration timeout = hce::chrono::duration());

    /**
     @brief cancel a running select wait()

     The caller is still responsible to await the awaitable (but it will return 
     immediately).
     */
    void cancel(const hce::awt<int>&);

private:
    template <typename CALLBACK, typename A, typename... As>
    auto package_callback(CALLBACK&& cb, A&& a, As&&... as) {
        // thunkize the result 
        return [a = std::forward<A>(a), 
                ...args = std::forward<Args>(args)]() mutable -> { 
            cb(std::forward<A>(a), std::forward<As>(as)...);
        };
    }

    struct no_callback_send : channel::detail::select::clause::send {
        no_callback_send(channel::base& ch, const channel::request& r);
        ~no_callback_send();
        void call();
    };

    struct no_callback_recv : channel::detail::select::clause::recv {
        no_callback_recv(channel::base& ch, const channel::request& r);
        ~no_callback_recv(){}
        void call();
    };

    template <typename CALLBACK>
    struct callback_send : channel::detail::select::clause::send {
        template<typename... As>
        callback_send(CALLBACK&& c, As&&... as) :
            channel::detail::select::clause::send(std::forward<As>(as)...),
            cb(std::forward<CALLBACK>(c))
        { }

        ~callback_send(){}
        inline void call() { cb(); }

        CALLBACK cb;
    };

    template <typename CALLBACK>
    struct callback_recv : channel::detail::select::clause::recv {
        template<typename... As>
        callback_recv(CALLBACK&& c, As&&... as) :
            channel::detail::select::clause::recv(std::forward<As>(as)...),
            cb(std::forward<CALLBACK>(c))
        { }

        ~callback_recv(){}
        inline void call() { cb(); }

        CALLBACK cb;
    };

    std::unique_ptr<channel::select::clause::base> make_send(
            channel::base& ch, const channel::request& r);

    template <typename A, typename... As>
    std::unique_ptr<channel::select::clause::base> make_send(
            channel::base& ch, const channel::request& r, A&& a, As&&... as)
    {
        return { 
            static_cast<channel::select::clause::base*>(new callback_send(
                package_callback(std::forward<A>(a), std::forward<As>(as)...),
                ch,
                r
            ))
        };
    }

    std::unique_ptr<channel::select::clause::base> make_recv(
            channel::base& ch, const channel::request& r);

    template <typename A, typename... As>
    std::unique_ptr<channel::select::clause::base> make_recv(
            channel::base& ch, const channel::request& r, A&& a, As&&... as)
    {
        return { 
            static_cast<channel::select::clause::base*>(new callback_recv(
                package_callback(std::forward<A>(a), std::forward<As>(as)...),
                ch,
                r
            ))
        };
    }

    channel::select::group group;
    std::vector<std::unique_ptr<clause::base>> clauses;
};

} // hce
