#include "hcec/hcec.h"
#include "hce/core.h"

#include "hce/logging.hpp"
#include "hce/owner.hpp"
#include "hce/lifecycle.hpp"
#include "hce/memory.hpp"
#include "hce/chrono.hpp"
#include "hce/scheduler.hpp"
#include "hce/threadpool.hpp"
#include "hce/blocking.hpp"
#include "hce/timer.hpp"
#include "hce/network.hpp"
#include "hce/module.h"

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

void hce_default_log_level() { return HCELOGLEVEL; }

void hce_lifecycle_config_initialize(hce_lifecycle_config_t* cfg) {
    if(!cfg) { return; }
    auto& c = *cfg;

    // log
    {
        c.log.loglevel = hce_default_log_level();
    }

    // allocator 
    {
        c.alloc.pool_allocator_default_block_limit =
            HCEPOOLALLOCATORDEFAULTBLOCKLIMIT;
    }

    // scheduler 
    {
        hce_scheduler_config_initialize(&(c.global_config));
        c.sch.global_config.loglevel = hce_default_log_level();
        c.sch.global_config.reusable_coroutine_handle_limit =
            HCEREUSABLECOROUTINEHANDLEGLOBALSCHEDULERLIMIT;
    }

    // threadpool 
    {
        c.tp.count = HCETHREADPOOLSCHEDULERCOUNT;
        hce_scheduler_config_initialize(&(c.tp.worker_config));
        c.tp.worker_config.loglevel = hce_default_log_level();
        c.tp.worker_config.reusable_coroutine_handle_limit =
            HCEREUSABLECOROUTINEHANDLETHREADPOOLLIMIT;
        c.tp.algorithm = &(hce::threadpool::lightest);
    }

    // blocking 
    {
        c.blk.reusable_block_worker_cache_size =
            HCEREUSABLEBLOCKWORKERCACHESIZE;
    }

    // timer
    {
        c.tmr.priority = []() -> int {
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
        }();

        c.tmr.busy_wait_threshold =
                HCETIMERBUSYWAITMICROSECONDTHRESHOLD;
        c.tmr.early_wakeup_threshold = 
                HCETIMEREARLYWAKEUPMICROSECONDTHRESHOLD;
        c.tmr.early_wakeup_long_threshold = 
                HCETIMEREARLYWAKEUPMICROSECONDLONGTHRESHOLD;
    }

    // network
    {
        c.tmr.thread_count = 1;
    }
}

hce_lifecycle_t* hce_lifecycle_initialize(hce_lifecycle_config_t* cfg) {
    return cfg 
        ? hce::lifecycle::initialize(*cfg).release()
        : hce::lifecycle::initialize().release();
}

void hce_lifecycle_destroy(hce_lifecycle_t* lifecycle) { 
    std::unique_ptr<hce::lifecycle> lf((hce::lifecycle*)v);
}

hce_scheduler_t* hce_global_scheduler() {
    return (hce_scheduler_t*)
        &(hce::service<hce::scheduler::global>::get().get_scheduler());
}

size_t hce_threadpool_scheduler_count() {
    return hce::service<hce::threadpool>::get().schedulers()size();
}

hce_scheduler_t* hce_threadpool_scheduler(size_t idx) {
    return (hce_scheduler_t*)
        hce::service<hce::threadpool>::get().schedulers()[idx].get();
}

void* hce_module_import(const char* filepath, void* context, hce_scheduler_t* sch) {
    return (void*)
        (hce::module::import(
            filepath, 
            context, 
            *((hce::scheduler*)sch)).release();
}

void* hce_module_import(const char* filepath, void* context) {
    return hce_module_import(filepath, context, hce_global_scheduler());
}

int hce_module_join(void* await) {
    return (int)hce::awt<int>((hce::awt<int>::interface*)await);
}
