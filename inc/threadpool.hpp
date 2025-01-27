//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#ifndef HERMES_COROUTINE_ENGINE_THREADPOOL
#define HERMES_COROUTINE_ENGINE_THREADPOOL

// c++
#include <vector>

// local
#include "base.hpp"
#include "logging.hpp"
#include "scheduler.hpp"

namespace hce {
namespace config {
namespace threadpool {

/**
 0: attempt to match scheduler count to the count of runtime detected CPU cores
 n: launch n-1 additional schedulers (the first index is always the global scheduler)

 The actual count of schedulers in the threadpool is guaranteed to be >=1.

 @return the configured scheduler count for this threadpool
 */
size_t count();

/**
 @brief provide the default threadpool scheduler configuration

 Similar to `hce::config::global_config()` but used for any 
 `scheduler`s spawned for the threadpool. This method's implementation can be 
 overridden by a user definition of global method 
 `hce::config::threadpool_config() at library compile time if compiler 
 define `HCECUSTOMCONFIG` is provided, otherwise a default implementation is 
 used.

 @return a copy of the global configuration 
 */
hce::config::scheduler::config config();

// Define a function pointer type that matches your function pointer
using algorithm_function_ptr = hce::scheduler& (*)();

/**
 @return an algorithm to be called by `hce::threadpool::algorithm()`
 */
algorithm_function_ptr algorithm();

}
}

namespace threadpool {

/**
 @brief an object providing access to a pool of worker schedulers 

 Operations which may benefit from being run in parallel (that is, running on 
 potentially different simultaneous processor cores) can be efficiently 
 scheduled using this mechanism.

 This mechanism employs *no* atomic locking by default after construction. Once 
 constructed, all members are threadsafe and read-only.

 The threadpool has a minimum size of 1, and the first scheduler in the 
 threadpool is always the default process wide scheduler returned by 
 `hce::scheduler::global::service::get().scheduler()`.

 The default count of workers can be configured at library compile time with 
 environment variable `HCETHREADPOOLSCHEDULERCOUNT`. If this value is undefined 
 or 0 then the framework will determine the count of worker threads (an attempt 
 is made to match the count of worker threads with the count of CPU cores).

 If `HCETHREADPOOLSCHEDULERCOUNT` is set to 1, no threads beyond the default 
 global scheduler (returned by 
 `hce::scheduler::global::service::get().scheduler()`) will be launched.

 If `HCETHREADPOOLSCHEDULERCOUNT` is set greater than 1, the additional count of 
 threads beyond the first will be launched.
 */
struct service : public printable {
    static inline std::string info_name() { return "hce::threadpool::service"; }
    inline std::string name() const { return service::info_name(); }

    inline std::string content() const {
        std::stringstream ss;
        auto it = schedulers_.cbegin();
        auto end = schedulers_.cend();

        ss << **it;
        ++it;

        while(it!=end) {
            ss << ", " << **it;
            ++it;
        }

        return ss.str(); 
    }

    /**
     There is only ever one threadpool in existence.

     @return the process wide threadpool service
     */
    static inline service& get() { return *(service::instance_); }

    /**
     @return a const reference to the managed vector of threadpool schedulers
     */
    inline const std::vector<std::shared_ptr<hce::scheduler>>& schedulers() const {
        return schedulers_;
    }

    /**
     Select a scheduler using the algorithm returned by 
     `hce::config::threadpool_algorithm()`.

     This operation is used by `hce::threadpool::schedule()`.

     @return a reference to a scheduler
     */
    inline hce::scheduler& algorithm() const { return algorithm_(); }

    /**
     @brief best effort mechanism to select the scheduler with the lightest workload 

     No atomic synchronization is used during this operation, aside from that 
     implicitly utilized by scheduler API. As such, this operation is low cost
     but only 'best effort'.

     This is the algorithm returned by the default implementation of 
     `hce::config::threadpool_algorithm()`

     @return a `scheduler`
     */
    static hce::scheduler& lightest(); 

private:
    service() : 
        // initialize const vector
        schedulers_([]() -> std::vector<std::shared_ptr<hce::scheduler>> { 
            // acquire the selected worker count from compiler define
            size_t worker_count = hce::config::threadpool::count();

            if(worker_count == 0) {
                // try to match worker_count to CPU count
                worker_count = std::thread::hardware_concurrency(); 

                // enforce a minimum of 1 worker threads
                if(worker_count == 0) { 
                    worker_count = 1; 
                }
            }

            // construct the initial vector given worker size
            std::vector<std::shared_ptr<hce::scheduler>> schedulers(worker_count);

            // the first scheduler is always the default global scheduler
            schedulers[0] = hce::scheduler::global::service::get().scheduler();

            // construct the rest of the schedulers
            for(size_t i=1; i<schedulers.size(); ++i) {
                // get an hce::scheduler::lifecycle
                auto lf = hce::scheduler::make(hce::config::threadpool::config());

                // assign the scheduler to the vector
                schedulers[i] = lf->scheduler();

                // register the worker lifecycle
                hce::scheduler::lifecycle::service::instance().registration(
                    std::move(lf));
            }

            // return the completed vector
            return schedulers;
        }())
    { 
        // set the threadpool's algorithm
        algorithm_ = hce::config::threadpool::algorithm();
        service::instance_ = this;
        HCE_HIGH_CONSTRUCTOR();
    }

    service(const service&) = delete;
    service(service&&) = delete;

    ~service(){ 
        HCE_HIGH_DESTRUCTOR();
        service::instance_ = nullptr; 
    }

    service& operator=(const service&) = delete;
    service& operator=(service&&) = delete;

    static service* instance_;

    const std::vector<std::shared_ptr<hce::scheduler>> schedulers_;
    hce::scheduler& (*algorithm_)();

    friend hce::lifecycle;
};

/**
 @brief call schedule() on a threadpool scheduler
 @param as arguments for scheduler::schedule()
 @return result of schedule()
 */
template <typename... As>
static inline auto schedule(As&&... as) {
    HCE_HIGH_FUNCTION_ENTER("hce::threadpool::schedule");
    return threadpool::service::get().algorithm().schedule(std::forward<As>(as)...);
}

}
}

#endif
