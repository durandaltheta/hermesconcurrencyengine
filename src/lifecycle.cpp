#include "lifecycle.hpp"

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
 long periods a smaller, sane maximum is suggested (supporting some arbitrary 
 count of pointer sized allocations). 
 
 4096 == sizeof(void*) * 64 on 64 bit systems
 */
#ifndef HCESYSTEMMEMORYBUCKETBYTELIMIT
#define HCESYSTEMMEMORYBUCKETBYTELIMIT sizeof(void*) * 64
#endif

/*
 The global scheduler is much more likely to have more coroutines 
 scheduled on it, as it is the scheduler selected by default. Additionally, 
 high level mechanisms prefer to keep scheduling on their current scheduler 
 (globally scheduled coroutines tend to call schedule() on the global 
 scheduler). Thus the global scheduler gets its own configured cache 
 resources, which is expected to be significantly larger than other 
 scheduler threads.

 131072 == sizeof(void*) * 2048 on 64 bit systems
 */
#ifndef HCEGLOBALMEMORYBUCKETBYTELIMIT
#define HCEGLOBALMEMORYBUCKETBYTELIMIT sizeof(void*) * 2048
#endif

/*
 65536 == sizeof(void*) * 1024 on 64 bit systems
 */
#ifndef HCESCHEDULERMEMORYBUCKETBYTELIMIT
#define HCESCHEDULERMEMORYBUCKETBYTELIMIT sizeof(void*) * 1024
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
#define HCETIMERSERVICEBUSYWAITMICROSECONDTHRESHOLD 5000
#endif

#ifndef HCETIMERSERVICEEARLYWAKEUPMICROSECONDTHRESHOLD
#define HCETIMERSERVICEEARLYWAKEUPMICROSECONDTHRESHOLD 10000
#endif

#ifndef HCETIMERSERVICEEARLYWAKEUPMICROSECONDLONGTHRESHOLD
#define HCETIMERSERVICEEARLYWAKEUPMICROSECONDLONGTHRESHOLD 250000
#endif

hce::lifecycle* hce::lifecycle::instance_ = nullptr;

hce::lifecycle::config::logging::logging() :
    loglevel(HCELOGLEVEL)
{ }

hce::lifecycle::config::memory::memory() :
    system(nullptr),
    global(nullptr),
    scheduler(nullptr),
    pool_allocator_default_block_limit(HCEPOOLALLOCATORDEFAULTBLOCKLIMIT)
{ }

hce::lifecycle::config::scheduler::scheduler() :
    default_resource_limit(HCESCHEDULERDEFAULTCOROUTINERESOURCELIMIT),
    global([]() -> hce::scheduler::config {
        hce::scheduler::config c;
        c.log_level = hce::logger::default_log_level();
        c.coroutine_resource_limit = HCEGLOBALSCHEDULERCOROUTINERESOURCELIMIT;
        return c;
    }()),
    standard([]() -> hce::scheduler::config {
        hce::scheduler::config c;
        c.log_level = hce::logger::default_log_level();
        c.coroutine_resource_limit = HCETHREADPOOLCOROUTINERESOURCELIMIT;
        return c;
    }())
{ }

hce::lifecycle::config::threadpool::threadpool() :
    count(HCETHREADPOOLSCHEDULERCOUNT),
    config([]() -> hce::scheduler::config {
        hce::scheduler::config c;
        c.log_level = hce::logger::default_log_level();
        c.coroutine_resource_limit = HCETHREADPOOLCOROUTINERESOURCELIMIT;
        return c;
    }()),
    algorithm(&(hce::threadpool::lightest))
{ }

hce::lifecycle::config::blocking::blocking() :
     process_worker_resource_limit(HCEPROCESSBLOCKWORKERRESOURCELIMIT),
     global_scheduler_worker_resource_limit(HCEGLOBALSCHEDULERBLOCKWORKERRESOURCELIMIT),
     default_scheduler_worker_resource_limit(HCEDEFAULTSCHEDULERBLOCKWORKERRESOURCELIMIT)
{ }

hce::lifecycle::config::timer::timer() :
    busy_wait_threshold(
        std::chrono::microseconds(
            HCETIMERSERVICEBUSYWAITMICROSECONDTHRESHOLD)),
    early_wakeup_threshold(
        std::chrono::microseconds(
            HCETIMERSERVICEEARLYWAKEUPMICROSECONDTHRESHOLD)),
    early_wakeup_long_threshold(
        std::chrono::microseconds(
            HCETIMERSERVICEEARLYWAKEUPMICROSECONDLONGTHRESHOLD)),
    algorithm(&(hce::timer::service::default_timeout_algorithm))
{ }

// provides default memory cache info implementations and necessary globals
struct info_impl {
    static hce::config::memory::cache::info& get() {
        // get the runtime thread type
        auto type = hce::config::memory::cache::info::thread::get_type();

        // select the proper implementation based on the thread type
        return type == hce::config::memory::cache::info::thread::type::system 
            ? *system_info_
            : type == hce::config::memory::cache::info::thread::type::global
                ? *global_info_
                : *scheduler_info_;
    }

    struct info_impl_ : public hce::config::memory::cache::info {
        info_impl_(const std::size_t bucket_count, const std::size_t byte_limit) { 
            // ensure the vector has enough memory for all the buckets via a
            // single allocation
            buckets_.reserve(bucket_count);

            for(std::size_t i=0; i<bucket_count; ++i) {
                std::size_t size = 1 << i;

                // sanitize byte limit on a per-bucket basis to ensure it's big 
                // enough for at least 1 element
                std::size_t bucket_byte_limit = byte_limit > size ? byte_limit : size;

                buckets_.push_back(
                    hce::config::memory::cache::info::bucket(
                        size, 
                        bucket_byte_limit / size));
            }
        }

        inline std::size_t count() const { return buckets_.size(); }

        inline hce::config::memory::cache::info::bucket& at(std::size_t idx) {
            return buckets_[idx];
        }

        inline hce::config::memory::cache::info::index_function indexer() {
            return &info_impl_::index_function_;
        }

    private:
        static inline std::size_t index_function_(const std::size_t size) {
            /*
             Refers to buckets that hold block sizes that are powers of 2 
             1, 2, 4, 8, 16, etc.

             This function when given a block size will return an index which 
             matches the vector returned from buckets() that can hold the block:
             0, 1, 2, 3, 4, etc.
             */

            // std::bit_width(size) returns 1-based index of the highest set bit
            int index = std::bit_width(size) - 1;

            // If size is not a power of two, increment index
            if ((size & (size - 1)) != 0) {
                ++index;
            }

            return index;
        }

        std::vector<hce::config::memory::cache::info::bucket> buckets_;
    };

    // the number of buckets in a memory cache, each holding allocated bytes 
    // of some power of 2
    static constexpr std::size_t bucket_count_ = HCETHREADLOCALMEMORYBUCKETCOUNT;

    // get the largest bucket block size, which is a power of 2
    static constexpr std::size_t largest_bucket_block_size_ = 
        1 << (HCETHREADLOCALMEMORYBUCKETCOUNT - 1);

    static constexpr std::size_t system_byte_limit_ = HCESYSTEMMEMORYBUCKETBYTELIMIT;
    static constexpr std::size_t global_byte_limit_ = HCEGLOBALMEMORYBUCKETBYTELIMIT;
    static constexpr std::size_t scheduler_byte_limit_ = HCESCHEDULERMEMORYBUCKETBYTELIMIT;

    static info_impl_ system_impl_;
    static info_impl_ global_impl_;
    static info_impl_ scheduler_impl_;
    static hce::config::memory::cache::info* system_info_;
    static hce::config::memory::cache::info* global_info_;
    static hce::config::memory::cache::info* scheduler_info_;
};
    
info_impl::info_impl_ 
info_impl::system_impl_(
    info_impl::bucket_count_, 
    info_impl::system_byte_limit_);

info_impl::info_impl_ 
info_impl::global_impl_(
    info_impl::bucket_count_, 
    info_impl::global_byte_limit_);

info_impl::info_impl_ 
info_impl::scheduler_impl_(
    info_impl::bucket_count_, 
    info_impl::scheduler_byte_limit_);

hce::config::memory::cache::info* info_impl::system_info_ = &(info_impl::system_impl_);
hce::config::memory::cache::info* info_impl::global_info_ = &(info_impl::global_impl_);
hce::config::memory::cache::info* info_impl::scheduler_info_ = &(info_impl::scheduler_impl_);

hce::config::memory::cache::info& hce::config::memory::cache::info::get() {
    return info_impl::get();
}

// allow for custom memory::cache configurations to be used
hce::lifecycle::memory_init::memory_init(const hce::lifecycle::config& c) {
    if(c.mem.system) {
        info_impl::system_info_ = c.mem.system;
    }

    if(c.mem.global) {
        info_impl::global_info_ = c.mem.global;
    }

    if(c.mem.scheduler) { 
        info_impl::system_info_ = c.mem.system;
    }
}
