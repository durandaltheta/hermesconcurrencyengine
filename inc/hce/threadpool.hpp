#pragma once
//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 

// c++
#include <vector>
#include <memory>

// local
#include "hcec/hcec_scheduler.h"
#include "hce/base.hpp"
#include "hce/owner.hpp"
#include "hce/service.hpp"
#include "hce/printable.hpp"
#include "hce/scheduler.hpp"
#include "hce/threadpool/config.hpp"

namespace hce {

/**
 @brief an object providing access to a pool of worker schedulers 

 Operations which may benefit from being run in parallel (that is, running on 
 potentially different simultaneous processor cores) can be efficiently 
 scheduled using this mechanism.

 This mechanism employs *no* atomic locking by default after construction. Once 
 constructed, all members are threadsafe and read-only.

 The threadpool has a minimum size of 1, and the first scheduler in the 
 threadpool is always the default process wide scheduler returned by 
 `hce::scheduler::global::service::get().get_scheduler()`.

 The default count of workers can be configured at library compile time with 
 environment variable `HCETHREADPOOLSCHEDULERCOUNT`. If this value is undefined 
 or 0 then the framework will determine the count of worker threads (an attempt 
 is made to match the count of worker threads with the count of CPU cores).

 If `HCETHREADPOOLSCHEDULERCOUNT` is set to 1, no threads beyond the default 
 global scheduler (returned by 
 `hce::scheduler::global::service::get().get_scheduler()`) will be launched.

 If `HCETHREADPOOLSCHEDULERCOUNT` is set greater than 1, the additional count of 
 threads beyond the first will be launched.
 */
struct threadpool : public hce::service<threadpool>, public hce::printable {
    static std::string info_name();
    std::string name() const;

    std::string content() const;

    /**
     @return a const reference to the managed vector of threadpool schedulers
     */
    const std::vector<std::shared_ptr<hce::scheduler>>& schedulers() const;

    /**
     Select a scheduler using the algorithm returned by 
     `hce::config::threadpool_algorithm()`.

     This operation is used by `hce::threadpool::schedule()`.

     @return a reference to a scheduler
     */
    hce::scheduler& algorithm() const;

    /**
     @brief best effort mechanism to select the scheduler with the lightest workload 

     No atomic synchronization is used during this operation, aside from that 
     implicitly utilized by scheduler API. As such, this operation is low cost
     but only 'best effort'.

     This is the algorithm returned by the default implementation of 
     `hce::config::threadpool_algorithm()`

     @return a `scheduler`
     */
    static hce_scheduler_t* lightest(); 

    /**
     @brief call schedule() on a threadpool scheduler
     @param as arguments for scheduler::schedule()
     @return result of schedule()
     */
    template <typename... As>
    static inline auto schedule(As&&... as) {
        return hce::service<threadpool>::get().algorithm().schedule(
                std::forward<As>(as)...);
    }

private:
    threadpool();
    threadpool(const threadpool&) = delete;
    threadpool(threadpool&&) = delete;
    virtual ~threadpool();

    threadpool& operator=(const threadpool&) = delete;
    threadpool& operator=(threadpool&&) = delete;

    const std::vector<std::shared_ptr<hce::scheduler>> schedulers_;
    hce::scheduler& (*algorithm_)();

    friend struct hce::owner<threadpool>;
};

}
