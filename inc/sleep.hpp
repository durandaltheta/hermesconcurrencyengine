//SPDX-License-Identifier: Apache-2.0
//Author: Blayne Dennis 
#ifndef __HERMES_COROUTINE_ENGINE_SLEEP__
#define __HERMES_COROUTINE_ENGINE_SLEEP__ 

// c++
#include <mutex>

// local
#include "atomic.hpp"
#include "coroutine.hpp"

namespace hce {

/// awaitable's result is true if timeout completed, else false
inline hce::awt<bool> sleep(const std::chrono::steady_clock::duration& dur) {
    struct ai : 
        public hce::scheduler::reschedule<
            hce::awaitable::lockable<
                hce::awt<bool>::interface,
                hce::spinlock>>
    {
        ai(const std::chrono::steady_clock::duration& dur) : 
            hce::scheduler::reschedule<
                hce::awaitable::lockable<
                    hce::awt<bool>::interface,
                    hce::spinlock>>(slk_,false),
            dur_(dur)
        { }

        virtual ~ai(){}

        inline bool on_ready() {
            scheduler::get().start(
                hce::timer::make(
                    dur_, 
                    [&](){ this->resume((void*)1) }, // timeout 
                    [&](){ this->resume((void*)0) }); // cancel
            return false;
        }

        inline void on_resume(void* m) { result_ = (bool)m; }
        inline bool get_result() { return result_; }

    private:
        std::chrono::steady_clock::duration dur_;
        bool result_ = false;
        hce::spinlock slk_;
    };

    return hce::awt<bool>(new ai(dur));
}

inline hce::awt<bool> sleep(timer::unit u, size_t count) {
    return sleep(timer::to_duration(u,count));
}

}

#endif
