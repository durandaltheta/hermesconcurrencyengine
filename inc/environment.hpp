//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#ifndef HERMES_COROUTINE_ENGINE_ENVIRONMENT
#define HERMES_COROUTINE_ENGINE_ENVIRONMENT

#include "logging.hpp"
#include "scheduler.hpp"
#include "threadpool.hpp"
#include "blocking.hpp"
#include "timer.hpp"
#include "lifecycle.hpp"

namespace hce {

struct environment : public printable {
    environment() { 
        HCE_MED_CONSTRUCTOR();
    }

    environment(const environment& rhs) { 
        copy(rhs);
        HCE_MED_CONSTRUCTOR();
    }

    virtual ~environment() {
        HCE_MED_DESTRUCTOR();
    }

    inline environment& operator=(const environment& rhs) {
        copy(rhs);
        HCE_MED_METHOD_ENTER("operator=");
        return *this;
    }

    static inline std::string info_name() { return "hce::environment"; }
    inline std::string name() const { return environment::info_name(); }

    /**
     @brief acquire all the service object pointers for the calling environment
     @return a constructed environment object
     */
    static inline environment clone() {
        return environment(
                hce::service<hce::thread::local>::ptr_ref(),
                hce::service<hce::scheduler::lifecycle::manager>::ptr_ref(),
                hce::service<hce::scheduler::global>::ptr_ref(),
                hce::service<hce::threadpool>::ptr_ref(),
                hce::service<hce::blocking>::ptr_ref(),
                hce::service<hce::timer>::ptr_ref(),
                hce::service<hce::lifecycle>::ptr_ref());
    }

    /**
     @brief install the internal environment

     Code compiled in totally separate units (such as in shared libraries) can 
     use this function to install a host environment's pointers into theirs.
     */
    inline void install() {
        hce::service<hce::thread::local>::ptr_ref() = local;
        hce::service<hce::scheduler::lifecycle::manager>::ptr_ref() = scheduler_lifecycle_manager;
        hce::service<hce::scheduler::global>::ptr_ref() = global_scheduler;
        hce::service<hce::threadpool>::ptr_ref() = threadpool;
        hce::service<hce::blocking>::ptr_ref() = blocking;
        hce::service<hce::timer>::ptr_ref() = timer;
        hce::service<hce::lifecycle>::ptr_ref() = lifecycle;
    }

private:
    environment(hce::thread::local* lc,
                hce::scheduler::lifecycle::manager* slm,
                hce::scheduler::global* glb,
                hce::threadpool* tp,
                hce::blocking* blk,
                hce::timer* tmr,
                hce::lifecycle* lf) :
        local(lc),
        scheduler_lifecycle_manager(slm),
        global_scheduler(glb),
        threadpool(tp),
        blocking(blk),
        timer(tmr),
        lifecycle(lf)
    { 
        HCE_MED_CONSTRUCTOR();
    }

    inline void copy(const environment& rhs) { 
        local = rhs.local;
        scheduler_lifecycle_manager = rhs.scheduler_lifecycle_manager;
        global_scheduler = rhs.global_scheduler;
        threadpool = rhs.threadpool;
        blocking = rhs.blocking;
        timer = rhs.timer;
        lifecycle = rhs.lifecycle;
    }

    hce::thread::local* local = nullptr;
    hce::scheduler::lifecycle::manager* scheduler_lifecycle_manager = nullptr;
    hce::scheduler::global* global_scheduler = nullptr;
    hce::threadpool* threadpool = nullptr;
    hce::blocking* blocking = nullptr;
    hce::timer* timer = nullptr;
    hce::lifecycle* lifecycle = nullptr;
};

}

#endif
