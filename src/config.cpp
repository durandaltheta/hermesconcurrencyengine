//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#include <cstring>
#include <memory>
#include <vector>

#include "loguru.hpp"
#include "utility.hpp"
#include "memory.hpp"
#include "scheduler.hpp"
#include "threadpool.hpp"
#include "channel.hpp"

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

// the limit of reusable block resources for in the global scheduler
#ifndef HCEGLOBALSCHEDULERBLOCKWORKERRESOURCELIMIT
#define HCEGLOBALSCHEDULERBLOCKWORKERRESOURCELIMIT 1
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

// the limit of reusable block resources for in threadpool schedulers
#ifndef HCETHREADPOOLBLOCKWORKERRESOURCELIMIT
#define HCETHREADPOOLBLOCKWORKERRESOURCELIMIT 0
#endif 

/// the default log initialization code is defined here
void hce::config::initialize_log() {
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

struct info_impl : public hce::config::memory::cache::info {
    info_impl(const size_t bucket_count, const size_t byte_limit) {
        for(size_t i=0; i<bucket_count; ++i) {
            size_t size = 1 << i;
            buckets_.push_back(
                hce::config::memory::cache::info::bucket(
                    size, 
                    byte_limit / size));
        }
    }

    inline size_t count() { return buckets_.size(); }

    inline hce::config::memory::cache::info::bucket& at(size_t idx) {
        return buckets_[idx];
    }

    static inline size_t index_function(size_t block) {
        /*
         Refers to buckets that hold block sizes that are powers of 2 
         1, 2, 4, 8, 16, etc.

         This function when given the above inputs will erturn an index which 
         matches the vector returned from buckets():
         0, 1, 2, 3, 4, etc.
         */

        // compute the bucket index using bit-width
        return std::bit_width(block - 1); // std::bit_width is C++20
    }

    inline hce::config::memory::cache::info::index_f indexer() {
        return &info_impl::index_function;
    }

private:
    std::vector<hce::config::memory::cache::info::bucket> buckets_;
};

hce::config::memory::cache::info&
hce::config::memory::cache::get_info() {
    static info_impl ii(HCETHREADLOCALMEMORYBUCKETCOUNT,
                        HCETHREADLOCALMEMORYBUCKETBYTELIMIT);
    return ii;
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
    config->block_worker_resource_limit = HCEGLOBALSCHEDULERBLOCKWORKERRESOURCELIMIT;
    return config;
}

std::unique_ptr<hce::scheduler::config> hce::config::threadpool::scheduler_config() {
    auto config = hce::scheduler::config::make();
    config->coroutine_resource_limit = HCETHREADPOOLCOROUTINERESOURCELIMIT;
    config->block_worker_resource_limit = HCETHREADPOOLBLOCKWORKERRESOURCELIMIT;
    return config;
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
