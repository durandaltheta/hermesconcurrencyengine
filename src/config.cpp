//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
/*
 This file contains the various `::config::` `extern` implementations necessary 
 for determining framework defaults. They are typically compiler define driven.
 */
#include <cstring>
#include <bit>
#include <memory>
#include <vector>
#include <stdexcept>

#include "loguru.hpp"
#include "utility.hpp"
#include "thread.hpp"
#include "memory.hpp"
#include "scheduler.hpp"
#include "timer.hpp"
#include "blocking.hpp"
#include "channel.hpp"
#include "threadpool.hpp"

#ifndef HCELOGLEVEL
/*
 Library compile time macro determining default printing log level. Default to 
 loguru::Verbosity_WARNING.
 */
#define HCELOGLEVEL -1
#endif 

// force a loglevel of loguru::Verbosity_OFF or higher
#if HCELOGLEVEL < -10 
#define HCELOGLEVEL loguru::Verbosity_OFF
#endif 

// force a loglevel of 9 or lower
#if HCELOGLEVEL > 9
#define HCELOGLEVEL 9
#endif 
    
#ifndef HCETHREADLOCALMEMORYBUCKETCOUNT
#define HCETHREADLOCALMEMORYBUCKETCOUNT 13
#endif 

#ifndef HCETHREADLOCALMEMORYBUCKETBYTELIMIT
#define HCETHREADLOCALMEMORYBUCKETBYTELIMIT 65536
#endif

// the default block limit in a pool_allocator
#ifndef HCEPOOLALLOCATORDEFAULTBLOCKLIMIT
#define HCEPOOLALLOCATORDEFAULTBLOCKLIMIT 64
#endif

// the default coroutine resource limit in a scheduler
#ifndef HCESCHEDULERDEFAULTCOROUTINERESOURCELIMIT
#define HCESCHEDULERDEFAULTCOROUTINERESOURCELIMIT HCEPOOLALLOCATORDEFAULTBLOCKLIMIT
#endif 

// the limit of reusable coroutine resources for in the global scheduler
#ifndef HCEGLOBALSCHEDULERCOROUTINERESOURCELIMIT
#define HCEGLOBALSCHEDULERCOROUTINERESOURCELIMIT HCESCHEDULERDEFAULTCOROUTINERESOURCELIMIT
#endif 

// the limit of reusable block workers shared among the entire process
#ifndef HCEPROCESSBLOCKWORKERRESOURCELIMIT 
#define HCEPROCESSBLOCKWORKERRESOURCELIMIT 1
#endif

// the limit of reusable block workers for the global scheduler cache
#ifndef HCEGLOBALSCHEDULERBLOCKWORKERRESOURCELIMIT
#define HCEGLOBALSCHEDULERBLOCKWORKERRESOURCELIMIT 1
#endif 

// the default limit of reusable block workers for scheduler caches
#ifndef HCEDEFAULTSCHEDULERBLOCKWORKERRESOURCELIMIT
#define HCEDEFAULTSCHEDULERBLOCKWORKERRESOURCELIMIT 0
#endif 

/*
 The count of threadpool schedulers. A value greater than 1 will cause the 
 threadpool to launch count-1 schedulers (the global scheduler is always the 
 first scheduler in the threadpool). A value of 0 allows the library to decide
 the total scheduler count. 
 */
#ifndef HCETHREADPOOLSCHEDULERCOUNT
#define HCETHREADPOOLSCHEDULERCOUNT 0
#endif

// the limit of reusable coroutine resources for in threadpool schedulers
#ifndef HCETHREADPOOLCOROUTINERESOURCELIMIT
#define HCETHREADPOOLCOROUTINERESOURCELIMIT HCESCHEDULERDEFAULTCOROUTINERESOURCELIMIT
#endif 

#ifndef HCETIMERSERVICEBUSYWAITMICROSECONDTHRESHOLD
#define HCETIMERSERVICEBUSYWAITMICROSECONDTHRESHOLD 3500
#endif

/// the default log initialization code is defined here
void hce::config::logger::initialize() {
    std::stringstream ss;
    ss << "-v" << HCELOGLEVEL;
    std::string process("hce");
    std::string verbosity = ss.str();
    std::vector<char*> argv;
    argv.push_back(process.data());
    argv.push_back(verbosity.data());
    int argc=argv.size();
    loguru::Options opt;
    opt.main_thread_name = nullptr;
    opt.signal_options = loguru::SignalOptions::none();
    loguru::init(argc, argv.data(), opt);
}

hce::config::memory::cache::info::thread::type& 
hce::config::memory::cache::info::thread::get_type() {
    thread_local hce::config::memory::cache::info::thread::type t = 
        hce::config::memory::cache::info::thread::type::system;
    return t;
}

struct info_impl {
    static hce::config::memory::cache::info& get() {
        // get the runtime thread type
        auto type = hce::config::memory::cache::info::thread::get_type();

        // select the proper implementation based on the thread type
        return type == hce::config::memory::cache::info::thread::type::system 
            ? system_impl_
            : type == hce::config::memory::cache::info::thread::type::global
                ? global_impl_
                : scheduler_impl_;
    }

private:
    struct info_impl_ : public hce::config::memory::cache::info {
        info_impl_(const size_t bucket_count, const size_t byte_limit) { 
            // ensure the vector has enough memory for all the buckets via a
            // single allocation
            buckets_.reserve(bucket_count);

            for(size_t i=0; i<bucket_count; ++i) {
                size_t size = 1 << i;

                // sanitize byte limit on a per-bucket basis to ensure it's big 
                // enough
                size_t bucket_byte_limit = byte_limit > size ? byte_limit : size;

                buckets_.push_back(
                    hce::config::memory::cache::info::bucket(
                        size, 
                        bucket_byte_limit / size));
            }
        }

        inline size_t count() const { return buckets_.size(); }

        inline hce::config::memory::cache::info::bucket& at(size_t idx) {
            return buckets_[idx];
        }

        inline hce::config::memory::cache::info::index_function indexer() {
            return &info_impl_::index_function_;
        }

    private:
        static inline size_t index_function_(size_t block) {
            /*
             Refers to buckets that hold block sizes that are powers of 2 
             1, 2, 4, 8, 16, etc.

             This function when given a block size will return an index which 
             matches the vector returned from buckets() that can hold the block:
             0, 1, 2, 3, 4, etc.
             */

            // std::bit_width(size) returns 1-based index of the highest set bit
            int index = std::bit_width(block) - 1;

            // If size is not a power of two, increment index
            if ((block & (block - 1)) != 0) {
                ++index;
            }

            return index;
        }

        std::vector<hce::config::memory::cache::info::bucket> buckets_;
    };

    // the number of buckets in a memory cache, each holding allocated bytes 
    // of some power of 2
    static constexpr size_t bucket_count_ = HCETHREADLOCALMEMORYBUCKETCOUNT;

    // get the largest bucket block size, which is a power of 2
    static constexpr size_t largest_bucket_block_size_ = 
        1 << (HCETHREADLOCALMEMORYBUCKETCOUNT - 1);

    /*
     System threads are much less likely to need large caches of 
     reusable memory because they are very unlikely to launch enough 
     tasks which consume and return this framework's memory resources (compared 
     to a scheduler which may have many concurrent (same operating system 
     thread) tasks utilizing said resources). 

     Additionally, there's a chance that a system thread may incidentally cache 
     values from other threads in a one-way relationship (for example, if 
     allocated values originate from a scheduler and are sent to particular 
     system thread regularly). This can cause odd imbalances of held memory 
     because an arbitrary system thread is less likely to consume its own cache.

     The allocating mechanisms which a system thread would likely use tends to 
     use pool_allocators, which already does reusable caching internally, and 
     will be just as effective most of the time for a system thread even if the 
     thread has a small memory cache (because pool_allocator allocation reuse 
     is tied to the object itself, not to a particular thread).
    
     To protect memory being cached unnecessarily in a system thread for 
     long periods we enforce it to a sane minimum (supporting some arbitrary 
     count of pointer sized allocations). On a 64 bit system the below should 
     resolve to 512. Any buckets which hold more than 512 bytes will hold only 
     1 cached instance of their block size.
     */
    static constexpr size_t system_byte_limit_ = sizeof(void*) * 64;

    /*
     In the scheduler default case we need to ensure the compile time 
     configured byte limit is at minimum at least as large as the largest 
     bucket's block size.
     */
    static constexpr size_t scheduler_byte_limit_ = 
        HCETHREADLOCALMEMORYBUCKETBYTELIMIT > largest_bucket_block_size_
          ? HCETHREADLOCALMEMORYBUCKETBYTELIMIT
          : largest_bucket_block_size_;

    /*
     The global scheduler is much more likely to have more coroutines 
     scheduled on it, as it is the scheduler selected by default. Additionally, 
     high level mechanisms prefer to keep scheduling on their current scheduler 
     (globally scheduled coroutines tend to call schedule() on the global 
     scheduler). Thus the global scheduler gets more cache resources.
     */
    static constexpr size_t global_byte_limit_ = scheduler_byte_limit_ * 2;

    static info_impl_ system_impl_;
    static info_impl_ scheduler_impl_;
    static info_impl_ global_impl_;
};
    
info_impl::info_impl_ 
info_impl::system_impl_(
    info_impl::bucket_count_, 
    info_impl::system_byte_limit_);

info_impl::info_impl_ 
info_impl::scheduler_impl_(
    info_impl::bucket_count_, 
    info_impl::scheduler_byte_limit_);

info_impl::info_impl_ 
info_impl::global_impl_(
    info_impl::bucket_count_, 
    info_impl::global_byte_limit_);

hce::config::memory::cache::info& hce::config::memory::cache::info::get() {
    return info_impl::get();
}

#ifdef _WIN32
int hce::config::timer::service::thread_priority() {
    return THREAD_PRIORITY_ABOVE_NORMAL;
}
#elif defined(_POSIX_VERSION)
int hce::config::timer::service::thread_priority() {
    static int priority = []{
        // Calculate the priority range
        int min_priority = sched_get_priority_min(SCHED_OTHER);
        int max_priority = sched_get_priority_max(SCHED_OTHER);

        // Calculate an intelligent high priority (somewhere near the maximum), 
        // 80% of max priority
        return min_priority + (max_priority - min_priority) * 0.8;  
    }();

    return priority;
}
#else 
int hce::config::timer::service::thread_priority() {
    return 0;
}
#endif 

size_t hce::config::timer::service::micro_busy_wait_threshold() {
    return HCETIMERSERVICEBUSYWAITMICROSECONDTHRESHOLD;
}

size_t hce::config::pool_allocator::default_block_limit() {
    return HCEPOOLALLOCATORDEFAULTBLOCKLIMIT;
}

size_t hce::config::scheduler::default_resource_limit() {
    return HCESCHEDULERDEFAULTCOROUTINERESOURCELIMIT;
}

std::unique_ptr<hce::scheduler::config> hce::config::global::scheduler_config() {
    auto config = hce::scheduler::config::make();
    config->coroutine_resource_limit = HCEGLOBALSCHEDULERCOROUTINERESOURCELIMIT;
    return config;
}

std::unique_ptr<hce::scheduler::config> hce::config::threadpool::scheduler_config() {
    auto config = hce::scheduler::config::make();
    config->coroutine_resource_limit = HCETHREADPOOLCOROUTINERESOURCELIMIT;
    return config;
}

size_t hce::blocking::config::process_worker_resource_limit() {
    return HCEPROCESSBLOCKWORKERRESOURCELIMIT;
}

size_t hce::blocking::config::global_scheduler_worker_resource_limit() {
    return HCEGLOBALSCHEDULERBLOCKWORKERRESOURCELIMIT;
}

size_t hce::blocking::config::default_scheduler_worker_resource_limit() {
    return HCEDEFAULTSCHEDULERBLOCKWORKERRESOURCELIMIT;
}

size_t hce::config::threadpool::scheduler_count() {
    return HCETHREADPOOLSCHEDULERCOUNT;
}

hce::config::threadpool::algorithm_function_ptr hce::config::threadpool::algorithm() {
    return &hce::threadpool::lightest;
}

size_t hce::config::channel::resource_limit() {
    if(hce::scheduler::in()) {
        return hce::scheduler::local().coroutine_resource_limit();
    } else {
        return hce::config::pool_allocator::default_block_limit();
    }
}
