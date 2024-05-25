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
inline hce::awaitable<bool> sleep(std::chrono::steady_clock::duration dur) {
    struct awt;

    struct tim : public hce::timer {
        tim(const std::chrono::steady_clock::duration& dur, 
                    awt* si) :
            hce::timer(dur),
            si_(si)
        { }

        virtual ~tim(){}
        inline coroutine cancel() { return tim::op(si_, 0);  }
        inline coroutine timeout() { return tim::op(si_, 1); }

        static inline coroutine op(awt* si, size_t val) {
            si->resume(val); 
            co_return;
        }

        awt* si_;
    };

    struct awt : protected hce::awaitable<bool>::implementation {
        virtual ~awt(){}

    protected:
        inline std::unique_lock<hce::spinlock>& get_lock() { return lk_; }

        inline bool ready_impl() {
            scheduler::get().start(
                hce::timer::make<tim>(dur, this));
            return false;
        }

        inline void resume_impl(void* m) { result_ = (bool)m; }
        inline bool result_impl() { return result_; }
    
        inline coroutine::destination acquire_destination() {
            return scheduler::reschedule{ this_scheduler() };
        }

    private:
        bool result_ = false;
        hce::spinlock slk_;
        std::unique_lock<spinlock> lk_(slk_);
    };

    return hce::awaitable<bool>::make<awt>();
}

inline hce::awaitable<bool> sleep(timer::unit u, size_t count) {
    return sleep(timer::to_duration(u,count));
}

}

#endif
