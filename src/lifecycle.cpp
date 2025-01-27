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
    
#ifndef HCEMEMORYCACHEBUCKETCOUNT
#define HCEMEMORYCACHEBUCKETCOUNT 13
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
#ifndef HCEMEMORYCACHESYSTEMBUCKETBYTELIMIT
#define HCEMEMORYCACHESYSTEMBUCKETBYTELIMIT sizeof(void*) * 64
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
#ifndef HCEMEMORYCACHEGLOBALBUCKETBYTELIMIT
#define HCEMEMORYCACHEGLOBALBUCKETBYTELIMIT sizeof(void*) * 2048
#endif

/*
 65536 == sizeof(void*) * 1024 on 64 bit systems
 */
#ifndef HCEMEMORYCACHESCHEDULERBUCKETBYTELIMIT
#define HCEMEMORYCACHESCHEDULERBUCKETBYTELIMIT sizeof(void*) * 1024
#endif

// the default block limit in a pool_allocator
#ifndef HCEPOOLALLOCATORDEFAULTBLOCKLIMIT
#define HCEPOOLALLOCATORDEFAULTBLOCKLIMIT 64
#endif

// the default coroutine resource limit in a scheduler
#ifndef HCEREUSABLECOROUTINEHANDLEDEFAULTSCHEDULERLIMIT
#define HCEREUSABLECOROUTINEHANDLEDEFAULTSCHEDULERLIMIT HCEPOOLALLOCATORDEFAULTBLOCKLIMIT
#endif 

// the limit of reusable coroutine resources for in the global scheduler
#ifndef HCEREUSABLECOROUTINEHANDLEGLOBALSCHEDULERLIMIT
#define HCEREUSABLECOROUTINEHANDLEGLOBALSCHEDULERLIMIT HCEREUSABLECOROUTINEHANDLEDEFAULTSCHEDULERLIMIT
#endif 

// the limit of reusable block workers shared among the entire process
#ifndef HCEPROCESSREUSABLEBLOCKWORKERPROCESSLIMIT
#define HCEPROCESSREUSABLEBLOCKWORKERPROCESSLIMIT 1
#endif

// the limit of reusable block workers for the global scheduler cache
#ifndef HCEREUSABLEBLOCKWORKERGLOBALSCHEDULERLIMIT
#define HCEREUSABLEBLOCKWORKERGLOBALSCHEDULERLIMIT 1
#endif 

// the default limit of reusable block workers for scheduler caches
#ifndef HCEREUSABLEBLOCKWORKERDEFAULTSCHEDULERLIMIT
#define HCEREUSABLEBLOCKWORKERDEFAULTSCHEDULERLIMIT 0
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
#ifndef HCEREUSABLECOROUTINEHANDLETHREADPOOLLIMIT
#define HCEREUSABLECOROUTINEHANDLETHREADPOOLLIMIT HCEREUSABLECOROUTINEHANDLEDEFAULTSCHEDULERLIMIT
#endif 

#ifndef HCETIMERBUSYWAITMICROSECONDTHRESHOLD
#define HCETIMERBUSYWAITMICROSECONDTHRESHOLD 5000
#endif

#ifndef HCETIMEREARLYWAKEUPMICROSECONDTHRESHOLD
#define HCETIMEREARLYWAKEUPMICROSECONDTHRESHOLD 10000
#endif

#ifndef HCETIMEREARLYWAKEUPMICROSECONDLONGTHRESHOLD
#define HCETIMEREARLYWAKEUPMICROSECONDLONGTHRESHOLD 250000
#endif

hce::spinlock hce::lifecycle::slk_;
hce::lifecycle* hce::lifecycle::instance_ = nullptr;

// define all the global (process-wide) configs
hce::lifecycle::config::logging hce::lifecycle::config::logging::global_;
hce::lifecycle::config::memory hce::lifecycle::config::memory::global_;
hce::lifecycle::config::allocator hce::lifecycle::config::allocator::global_;
hce::lifecycle::config::scheduler hce::lifecycle::config::scheduler::global_(
    hce::lifecycle::config::memory::global_);
hce::lifecycle::config::threadpool hce::lifecycle::config::threadpool::global_(
    hce::lifecycle::config::memory::global_);
hce::lifecycle::config::blocking hce::lifecycle::config::blocking::global_;
hce::lifecycle::config::timer hce::lifecycle::config::timer::global_;

hce::lifecycle::config::logging::logging() :
    loglevel(HCELOGLEVEL)
{ }

// provides default memory cache info implementations and necessary globals
struct info {
    struct impl : public hce::config::memory::cache::info {
        impl(const char* name, const std::size_t bucket_count, const std::size_t byte_limit) :
            name_(name)
        { 
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

        const char* name() const { return name_; }

        std::size_t count() const { return buckets_.size(); }

        hce::config::memory::cache::info::bucket& at(std::size_t idx) {
            return buckets_[idx];
        }

    private:
        const char* name_;
        std::vector<hce::config::memory::cache::info::bucket> buckets_;
    };

    // the number of buckets in a memory cache, each holding allocated bytes 
    // of some power of 2
    static constexpr std::size_t bucket_count_ = HCEMEMORYCACHEBUCKETCOUNT;

    // get the largest bucket block size, which is a power of 2
    static constexpr std::size_t largest_bucket_block_size_ = 
        1 << (HCEMEMORYCACHEBUCKETCOUNT - 1);

    static constexpr std::size_t system_byte_limit_ = HCEMEMORYCACHESYSTEMBUCKETBYTELIMIT;
    static constexpr std::size_t global_byte_limit_ = HCEMEMORYCACHEGLOBALBUCKETBYTELIMIT;
    static constexpr std::size_t scheduler_byte_limit_ = HCEMEMORYCACHESCHEDULERBUCKETBYTELIMIT;

    static hce::config::memory::cache::info::index_t indexer(const std::size_t size) {
        /*
         Refers to buckets that hold block sizes that are powers of 2 
         1, 2, 4, 8, 16, etc.

         This function when given a block size will return an index which 
         matches the vector returned from buckets() that can hold the block:
         0, 1, 2, 3, 4, etc.
         */

        // std::bit_width(size) returns 1-based index of the highest set bit
        hce::config::memory::cache::info::index_t index = std::bit_width(size) - 1;

        // If size is not a power of two, increment index
        if ((size & (size - 1)) != 0) {
            ++index;
        }

        return index;
    }

    static impl system_impl_;
    static impl global_impl_;
    static impl scheduler_impl_;
};
    
info::impl info::system_impl_("system", info::bucket_count_, info::system_byte_limit_);
info::impl info::global_impl_("global", info::bucket_count_, info::global_byte_limit_);
info::impl info::scheduler_impl_("scheduler", info::bucket_count_, info::scheduler_byte_limit_);

hce::config::scheduler::config::config() :
    loglevel(HCELOGLEVEL),
    reusable_coroutine_handle_limit(HCEREUSABLECOROUTINEHANDLEDEFAULTSCHEDULERLIMIT),
    // pull directly from the global memory config
    cache_info(hce::lifecycle::config::memory::global_.scheduler) 
{ }

hce::lifecycle::config::memory::memory() :
    indexer(&(info::indexer)),
    system(&(info::system_impl_)),
    global(&(info::global_impl_)),
    scheduler(&(info::scheduler_impl_))
{ }

hce::lifecycle::config::allocator::allocator() :
    pool_allocator_default_block_limit(HCEPOOLALLOCATORDEFAULTBLOCKLIMIT)
{ }

hce::lifecycle::config::scheduler::scheduler(const hce::lifecycle::config::memory& m) :
    global_config([&]() -> hce::config::scheduler::config {
        hce::config::scheduler::config c;
        c.loglevel = HCELOGLEVEL;
        c.reusable_coroutine_handle_limit = HCEREUSABLECOROUTINEHANDLEGLOBALSCHEDULERLIMIT;
        c.cache_info = m.global;
        return c;
    }())
{ }

hce::lifecycle::config::threadpool::threadpool(const hce::lifecycle::config::memory& m) :
    count(HCETHREADPOOLSCHEDULERCOUNT),
    worker_config([&m]() -> hce::config::scheduler::config {
        hce::config::scheduler::config c;
        c.loglevel = HCELOGLEVEL;
        c.reusable_coroutine_handle_limit = HCEREUSABLECOROUTINEHANDLETHREADPOOLLIMIT;
        c.cache_info = m.scheduler;
        return c;
    }()),
    algorithm(&(hce::threadpool::service::lightest))
{ }

hce::lifecycle::config::blocking::blocking() :
     reusable_block_worker_cache_size(HCEREUSABLEBLOCKWORKERCACHESIZE)
{ }

hce::lifecycle::config::timer::timer() :
    priority([]() -> int {
#ifdef _WIN32
            return THREAD_PRIORITY_ABOVE_NORMAL;
#elif defined(_POSIX_VERSION)
            // Calculate the priority range
            int min_priority = sched_get_priority_min(SCHED_OTHER);
            int max_priority = sched_get_priority_max(SCHED_OTHER);

            // Calculate an intelligent high priority (somewhere near the maximum), 
            // 80% of max priority
            return min_priority + (max_priority - min_priority) * 0.8;  
#else 
            return 0;
#endif 
    }()),
    busy_wait_threshold(
        std::chrono::microseconds(
            HCETIMERBUSYWAITMICROSECONDTHRESHOLD)),
    early_wakeup_threshold(
        std::chrono::microseconds(
            HCETIMEREARLYWAKEUPMICROSECONDTHRESHOLD)),
    early_wakeup_long_threshold(
        std::chrono::microseconds(
            HCETIMEREARLYWAKEUPMICROSECONDLONGTHRESHOLD)),
    algorithm(&(hce::timer::service::default_timeout_algorithm))
{ }

hce::lifecycle::logging_init::logging_init() {
    struct do_once {
        do_once() {
            std::stringstream ss;
            ss << "-v" << hce::config::logging::default_log_level();
            std::string process("hce");
            std::string verbosity = ss.str();

            // Create raw char pointers for argc/argv
            const char* argv[] = {process.c_str(), verbosity.c_str(), nullptr};
            int argc = 2; // Number of actual arguments (excluding the nullptr)

            loguru::Options opt;
            opt.main_thread_name = nullptr;
            opt.signal_options = loguru::SignalOptions::none();
            loguru::init(argc, const_cast<char**>(argv), opt);
        }
    };

    static do_once d; // static so init only happens once
}
