//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#ifndef HERMES_COROUTINE_ENGINE_INSTALL
#define HERMES_COROUTINE_ENGINE_INSTALL

#include <memory>

#include "logging.hpp"
#include "coroutine.hpp"
#include "scheduler.hpp"
#include "threadpool.hpp"
#include "blocking.hpp"
#include "timer.hpp"

namespace hce {

/**
 @brief an object capable of representing and installing an hce environment

 If some code is compiled seperately with the `hce` framework (such as in a 
 shared library), it can inherit the main process' environment (created and 
 maintained by an `hce::lifecycle` object) using this object. The environment 
 can be installed into the seperately compiled code, allowing it to access the 
 framework services:
 - scheduler lifecycle service
 - global scheduler service
 - threadpool service
 - blocking service
 - timer service
 */
struct environment : public printable {
    ~environment() { HCE_HIGH_DESTRUCTOR(); }

    /**
     @brief copy the calling environment
     @return an allocated environment object containing the necessities to install
     */
    inline static std::unique_ptr<environment> clone();

    static inline std::string info_name() { return "hce::environment"; }
    inline std::string name() const { return environment::info_name(); }

    inline std::string content() const {
        std::stringstream ss;
        ss << scheduler_lifecycle_service_ << ", "
           << scheduler_global_service_ << ", "
           << threadpool_service_ << ", "
           << blocking_service_ << ", "
           << timer_service_;
        return ss.str();
    }

    /**
     @brief install the environment into the calling context 

     A valid calling context might be a dynamically linked library that is 
     compiled to use this framework but maintains no `hce::lifecycle` object. 
     */
    inline void install();

private:
    environment(hce::scheduler::lifecycle::service* slfs,
                hce::scheduler::global::service* glos,
                hce::threadpool::service* thds,
                hce::blocking::service* blks,
                hce::timer::service* tims) :
        scheduler_lifecycle_service_(slfs),
        scheduler_global_service_(glos),
        threadpool_service_(thds),
        blocking_service_(blks),
        timer_service_(tims)
    { 
        HCE_HIGH_CONSTRUCTOR();
    }

    hce::scheduler::lifecycle::service* scheduler_lifecycle_service_;
    hce::scheduler::global::service* scheduler_global_service_;
    hce::threadpool::service* threadpool_service_;
    hce::blocking::service* blocking_service_;
    hce::timer::service* timer_service_;
};

}

#endif
