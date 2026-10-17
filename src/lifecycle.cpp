#include "utility.hpp"
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

// called once at process start
struct hce_log_initializer {
    hce_log_initializer() {
        int loglevel = HCELOGLEVEL;
        std::stringstream ss;
        ss << "-v" << loglevel;
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
} g_hce_log_initializer; 

hce::lifecycle::config::logging::logging() :
    loglevel(HCELOGLEVEL)
{ }

hce::config::scheduler::config::config() :
    loglevel(HCELOGLEVEL),
    reusable_coroutine_handle_limit(HCEREUSABLECOROUTINEHANDLEDEFAULTSCHEDULERLIMIT)
{ }

hce::lifecycle::config::allocator::allocator() :
    pool_allocator_default_block_limit(HCEPOOLALLOCATORDEFAULTBLOCKLIMIT)
{ }

hce::lifecycle::config::scheduler::scheduler() :
    global_config([&]() -> hce::config::scheduler::config {
        hce::config::scheduler::config c;
        c.loglevel = HCELOGLEVEL;
        c.reusable_coroutine_handle_limit = HCEREUSABLECOROUTINEHANDLEGLOBALSCHEDULERLIMIT;
        return c;
    }())
{ }

hce::lifecycle::config::threadpool::threadpool() :
    count(HCETHREADPOOLSCHEDULERCOUNT),
    worker_config([]() -> hce::config::scheduler::config {
        hce::config::scheduler::config c;
        c.loglevel = HCELOGLEVEL;
        c.reusable_coroutine_handle_limit = HCEREUSABLECOROUTINEHANDLETHREADPOOLLIMIT;
        return c;
    }()),
    algorithm(&(hce::threadpool::lightest))
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
    algorithm(&(hce::timer::default_timeout_algorithm))
{ }

std::unique_ptr<hce::lifecycle> hce::lifecycle::initialize(hce::lifecycle::config c) {
    return std::unique_ptr<hce::lifecycle>(
        new lifecycle(
            c, 
            // initialize thread locals first
            std::unique_ptr<hce::thread::local>(new hce::thread::local)));
}

int hce::config::logging::default_log_level() { return HCELOGLEVEL; }

hce::lifecycle::~lifecycle() { HCE_INFO_DESTRUCTOR(); }

std::string hce::lifecycle::info_name() { return "hce::lifecycle"; }
std::string hce::lifecycle::name() const { return lifecycle::info_name(); }

/// return the lifecycle's config
const hce::lifecycle::config& hce::lifecycle::get_config() { return config_; }

// initialize hce::thread::local before lifecycle so logging can function
hce::lifecycle::lifecycle(const config& c, std::unique_ptr<hce::thread::local>&& l) : 
    config_(c),
    local_(std::move(l))
{ 
    HCE_INFO_CONSTRUCTOR(); 
}
