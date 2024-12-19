//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#include <sstream>

#include "threadpool.hpp"

std::string hce::threadpool::content() const { 
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
    
hce::threadpool& hce::threadpool::get() {
    static threadpool tp;
    return tp;
}
    
hce::threadpool::threadpool() :
    // initialize const vector
    schedulers_([]() -> std::vector<std::shared_ptr<hce::scheduler>> { 
        // acquire the selected worker count from compiler define
        size_t worker_count = hce::config::threadpool::scheduler_count();

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

        // first scheduler is always the default global scheduler
        schedulers[0] = hce::scheduler::global();

        // construct the rest of the schedulers
        for(size_t i=1; i<schedulers.size(); ++i) {
            // get an hce::scheduler::lifecycle
            auto lf = hce::scheduler::make(
                hce::config::threadpool::scheduler_config());

            // assign the scheduler to the vector
            schedulers[i] = lf->scheduler();

            // register the worker lifecycle
            hce::scheduler::lifecycle::manager::instance().registration(
                std::move(lf));
        }

        // return the completed vector
        return schedulers;
    }())
{ 
    // set the threadpool's algorithm
    algorithm_ = hce::config::threadpool::algorithm();
}
    
hce::scheduler& hce::threadpool::lightest() {
    /*
     A thread_local index is used to determine which scheduler to check first 
     during scheduler() selection. This value rotates through available indexes 
     to limit lock contention on workers earlier in the vector of schedulers, 
     distributing contention amongst all worker threads.

     A thread_local rotatable start index is used as a "best effort" mechanism 
     to eliminate the need for a global lock or global atomic value (which would 
     become a new bottleneck for lock contention). It is assumed that any lock 
     contention caused by the same worker accidentally being accessed from
     different threads will normalize over time while simultaneously preventing 
     a bottleneck in scenarios with *many* worker threads executing in parallel.
     */
    thread_local size_t tl_rotatable_start_index = 0;
    const auto& schedulers = hce::threadpool::get().schedulers();
    const size_t starting_index = tl_rotatable_start_index;
    const size_t worker_count = schedulers.size();

    // rotate the thread_local start index for the next call to this function
    if(tl_rotatable_start_index < worker_count - 1) [[likely]] {
        ++tl_rotatable_start_index;
    } else {
        tl_rotatable_start_index = 0;
    }

    size_t index = starting_index;

    // get initial scheduler information
    hce::scheduler* lightest_scheduler = schedulers[index].get();
    size_t lightest_workload = lightest_scheduler->scheduled_count();

    // return immediately if no workload or if only 1 worker
    if(lightest_workload && worker_count > 1) [[likely]] {
        // trivially inlined
        auto iterate_workloads = [&](const size_t limit){
            // Iterate index to the end of the workers
            for(; index < limit; ++index) [[likely]] {
                hce::scheduler* current_scheduler = schedulers[index].get();
                size_t current_workload = current_scheduler->scheduled_count();

                if(current_workload) [[likely]] {
                    if(current_workload < lightest_workload) {
                        lightest_scheduler = current_scheduler;
                        lightest_workload = current_workload;
                    }
                } else [[unlikely]] {
                    // return immediately if no workload
                    lightest_scheduler = current_scheduler;
                    lightest_workload = current_workload;
                    break;
                }
            }
        };

        // begin iteration at the next worker
        ++index;

        // Iterate index to the end of the workers
        iterate_workloads(worker_count);

        // return without further iteration if no workload
        if(lightest_workload) {
            // Rotate index back to 0 and end iteration where we started
            index = 0;
            iterate_workloads(starting_index);
        }
    }

    return *lightest_scheduler;
}
