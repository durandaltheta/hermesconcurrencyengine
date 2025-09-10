//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#ifndef HERMES_COROUTINE_ENGINE_LIFECYCLE
#define HERMES_COROUTINE_ENGINE_LIFECYCLE

#include <memory>
#include <string>
#include <sstream>
#include <exception>
#include <mutex>
#include <thread>
#include <map>

#include "loguru.hpp"
#include "logging.hpp"
#include "memory.hpp"
#include "chrono.hpp"
#include "scheduler.hpp"
#include "threadpool.hpp"
#include "blocking.hpp"
#include "timer.hpp"

namespace hce {

/**
 @brief RAII configuration and management object for this framework

 The instance of this object configures the framework and constructs, allocates 
 and maintains all singleton services.
 */
struct lifecycle : public service<lifecycle>, public hce::printable {
    /**
     @brief configuration for the framework 

     The user can customize these options at runtime and pass the result to 
     `hce::lifecycle::initialize()` to set the process-wide configuration.
     Reads to process-wide configuration values are lockless.

     Default values are determined by compiler defines (see below). 
     */
    struct config {
        struct logging {
            logging();

            /**
             @brief runtime default log level

             Defaults set by compiler define(s):
             HCELOGLEVEL
             */
            int loglevel; 
        };

        struct allocator {
            allocator();

            /**
             @brief pool_allocator's default cache size

             This value sets the default cache for pool allocator's reusable 
             cached memory count.

             Defaults set by compiler define(s):
             HCEPOOLALLOCATORDEFAULTCACHESIZE
             */
            size_t pool_allocator_default_cache_size; 
        };

        struct scheduler {
            scheduler();

            /**
             @brief global scheduler config

             This is defaulted to a valid implementation.

             Defaults set by compiler define(s):
             HCEGLOBALSCHEDULERCOROUTINERESOURCECACHE
             */
            hce::config::scheduler::config global_config; 
        };

        struct threadpool {
            threadpool();

            /**
             @brief count of worker schedulers in the threadpool 

             0: attempt to match scheduler count to the count of runtime detected CPU cores
             n: launch n-1 additional schedulers (the first index is always the global scheduler)

             The actual count of schedulers in the threadpool is guaranteed to be >=1.

             Defaults set by compiler define(s):
             HCETHREADPOOLSCHEDULERCOUNT
             */
            size_t count; 

            /**
             @brief threadpool scheduler config

             This is defaulted to a valid implementation.

             Defaults set by compiler define(s):
             HCETHREADPOOLCOROUTINERESOURCECACHE
             */
            hce::config::scheduler::config worker_config; 

            /**
             @brief threadpool worker selector algorithm

             This is defaulted to a valid implementation if none is provided.
             */
            hce::config::threadpool::algorithm_function_ptr algorithm; 
        };

        struct blocking {
            blocking();

            /**
             @brief reusable block workers count shared by the process

             Defaults set by compiler define(s):
             HCEPROCESSREUSABLEBLOCKWORKERPROCESSCACHE
             */
            size_t reusable_block_worker_cache_size;
        };

        struct timer {
            timer();

            int priority;

            /**
             @brief threshold used by default timeout algorithm for when to busy-wait

             If the difference between the current time and the nearest timer 
             timeout is under this threshold, the timer thread will busy-wait 
             until timeout for increased precision.

             Defaults set by compiler define(s):
             HCETIMERBUSYWAITMICROSECONDTHRESHOLD
             */
            hce::chrono::duration busy_wait_threshold;

            /**
             @brief threshold used by default timeout algorithm for the early-wakeup duration

             How this value is used is determined by the timeout algorithm.

             Defaults set by compiler define(s):
             HCETIMEREARLYWAKEUPMICROSECONDTHRESHOLD
             */
            hce::chrono::duration early_wakeup_threshold;

            /**
             @brief threshold used by default timeout algorithm for when to begin early-wakeups for long running timers

             How this value is used is determined by the timeout algorithm.

             Defaults set by compiler define(s):
             HCETIMEREARLYWAKEUPMICROSECONDLONGTHRESHOLD
             */
            hce::chrono::duration early_wakeup_long_threshold;

            /**
             @brief timer service timeout algorithm

             Algorithm for calculating early timeouts for the timer service.
             The timer service will override the result of this algorithm if 
             the returned timeout exceeds the timeout of the nearest timer 
             timeout.

             This is defaulted to a valid implementation if none is provided.
             */
            hce::config::timer::algorithm_function_ptr algorithm;
        };

        config();

        logging log;
        allocator alloc;
        scheduler sch;
        threadpool tp;
        blocking blk;
        timer tmr;
    };

    virtual ~lifecycle();

    /**
     @brief set the hce framework's global configuration and allocate, construct and start the framework 

     Argument `hce::lifecycle::config` configures the process-wide framework 
     configuration, which is utilized by all `hce::config::` namespace 
     operations. Access to the process-wide configuration is lockless.

     The returned lifecycle object starts the hce framework and manages its 
     memory. When the returned lifecycle object goes out scope the hce framework 
     will be shutdown and destroyed. 

     All other features rely on this object staying in existence. Therefore, all 
     launched operations (*INCLUDING* all `hce::memory::` based deallocations) 
     must complete and join before this object can safely go out of existence. 
     Failure to do this can cause memory exceptions or deadlock.

     There can only be one lifecycle in existence at a time. If this is called 
     again while the first one still exists, the process will exit. It is also 
     not recommended to continually create and destroy lifecycles, instead to 
     keep one in existence until process exit. Recommended design is to hold 
     this pointer on the `main()` thread's stack at a point before any `hce::`
     operations are launched.

     @param c optional framework configuration
     @return a lifecycle object managing the memory of the hce framework
     */
    static std::unique_ptr<hce::lifecycle> initialize(config c = {});
    static std::string info_name();
    std::string name() const;

    /// return the lifecycle's config
    const config& get_config();

private:
    // initialize hce::thread::local before lifecycle so logging can function
    lifecycle(const config& c);

    std::mutex mtx_;

    // global configuration for the framework
    config config_;

    // the various services, in order of dependencies
    hce::scheduler::lifecycle::manager scheduler_lifecycle_manager_;
    hce::scheduler::global scheduler_global_;
    hce::threadpool threadpool_;
    hce::blocking::manager blocking_manager_;
    hce::timer timer_;
};

/**
 @brief a convenience for calling `hce::lifecycle::initialize()`
 @returns the newly allocated and constructed process `hce::lifecycle` pointer
 */
inline std::unique_ptr<lifecycle> initialize() {
    return hce::lifecycle::initialize();
}

}

#endif
