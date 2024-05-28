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
inline hce::awaitable<bool> sleep(const std::chrono::steady_clock::duration& dur) {
    struct awt : public hce::awaitable<bool>::implementation {
        awt(const std::chrono::steady_clock::duration& dur) : dur_(dur) { }
        virtual ~awt(){}

    protected:
        inline std::unique_lock<hce::spinlock>& get_lock() { return lk_; }

        inline bool ready_impl() {
            scheduler::get().start(
                hce::timer::make(
                    dur_, 
                    [&](){ this->resume((void*)1) }, // timeout 
                    [&](){ this->resume((void*)0) }); // cancel
            return false;
        }

        inline void resume_impl(void* m) { result_ = (bool)m; }
        inline bool result_impl() { return result_; }
    
        inline base_coroutine::destination acquire_destination() {
            return scheduler::reschedule{ scheduler::local() };
        }

    private:
        std::chrono::steady_clock::duration dur_;
        bool result_ = false;
        hce::spinlock slk_;
        std::unique_lock<spinlock> lk_(slk_);
    };

    return hce::awaitable<bool>::make<awt>(dur);
}

inline hce::awaitable<bool> sleep(timer::unit u, size_t count) {
    return sleep(timer::to_duration(u,count));
}

}

#endif
