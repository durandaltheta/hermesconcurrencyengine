//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#include <sstream>

#include "threadpool.hpp"
#include "lifecycle.hpp"
    
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
    const auto& schedulers = hce::service<hce::threadpool>::get().schedulers();
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
