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
     auto timeout = hce::chrono::milliseconds(1000);

     while(true) {
         // build our clauses each time
         select.recv(ich, i); // gets index 0
         select.send(sch, s); // gets index 1
         auto result = co_await select.wait(timeout);

         switch(result.index) {
             case 0:
                 std::cout << "received int: " << i << std::endl;
                 break;
             case 1:
                 std::cout << "sent std::string: " << s << std::endl;
                 co_return true;
             default:
                 std::cout << "an error occurred: " << result.index << std::endl;
                 co_return false;
         }
     }
 }
 ```

 Because the above example uses a switch statement, each case remains as part 
 of the coroutine, and allows the user to use coroutine keywords.
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
        // handle result.status()
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
     @return the index of the clause
     */
    template <typename T>
    int send(channel::interface<T>& ch, T&& src) {
        channel::request::op_e op;

        if constexpr (std::is_rvalue_reference_v<decltype(src)>) {
            op = channel::select::request::op_e::send_shallow;
        } else {
            op = channel::select::request::op_e::send_deep;
        }

        int idx = static_cast<int>(clauses.size());
        channel::request r{ op, &src, &group, idx };

        return clause({ make_send(ch, r) });
    }

    /**
     @brief register a receive clause and return the index 

     WARNING: callbacks are NOT coroutines.

     @param ch the channel to receive from 
     @param dest the channel output to write to 
     @return the index of the clause
     */
    template <typename T>
    int recv(channel::interface<T>& ch, T& dest) {
        int idx = static_cast<int>(clauses.size());

        channel::request r{ 
            channel::request::op_e::recv, 
            &dest, 
            &group, 
            idx 
        };

        return clause({ make_recv(ch, r) });
    }

    /**
     @brief register some clause with the select
     */
    int clause(std::unique_ptr<channel::select::clause::base> cb);

    /**
     Returns an awaitable that parks until exactly one case wins.

     The select object must remain alive until the awaitable completes.
     Result is the winning case index, or a negative index on cancel or timeout.

     @param timeout optional timeout duration 
     @return an awaitable to block until a clause wins, timeout or cancel
     */
    hce::awt<result> wait(hce::chrono::duration timeout = hce::chrono::duration());

    /**
     @brief cancel a running select wait()

     The caller is still responsible to await the awaitable (but it will return 
     immediately).
     */
    void cancel(const hce::awt<int>&);

private:
    std::unique_ptr<channel::select::clause::base> make_send(
            channel::base& ch, const channel::request& r);

    std::unique_ptr<channel::select::clause::base> make_recv(
            channel::base& ch, const channel::request& r);

    std::size_t activate_index = 0;
    channel::select::group group;
    std::vector<std::unique_ptr<clause::base>> clauses;
};

} // hce
