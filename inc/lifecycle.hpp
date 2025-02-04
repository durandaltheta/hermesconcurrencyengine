//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#ifndef HERMES_COROUTINE_ENGINE_LIFECYCLE
#define HERMES_COROUTINE_ENGINE_LIFECYCLE

#include <memory>
#include <string>
#include <sstream>
#include <exception>
#include <mutex>
#include <thread>
#include <map>

#include "loguru.hpp"
#include "logging.hpp"
#include "memory.hpp"
#include "chrono.hpp"
#include "scheduler.hpp"
#include "threadpool.hpp"
#include "blocking.hpp"
#include "timer.hpp"

namespace hce {

/**
 @brief RAII configuration and management object for this framework

 The instance of this object configures the framework and constructs, allocates 
 and maintains all singleton services and thread memory caches.
 */
struct lifecycle : public printable {
private:
    struct config_init;

public:
    struct cannot_register_cache_without_lifecycle : public std::exception { 
        inline const char* what() const noexcept { 
            return "hce::lifecycle does not exist, cannot create hce::memory::cache";
        }
    };

    struct cache_already_registered : public std::exception {
        cache_already_registered(const std::thread::id& key) :
            estr([&]() -> std::string {
                std::stringstream ss;
                ss << "failed to register hce::memory::cache in the hce::lifecycle because key["
                   << key
                   << "] is already in use";
                return ss.str();
            }())
        { }

        inline const char* what() const noexcept { return estr.c_str(); }

    private:
        const std::string estr;
    };

    /**
     @brief configuration for the framework 

     The user can customize these options at runtime and pass the result to 
     `hce::lifecycle::initialize()` to set the process-wide configuration.
     Reads to process-wide configuration values are lockless.

     Default values are determined by compiler defines (see below). 
     */
    struct config {
        struct scheduler;
        struct threadpool;

        struct logging {
            logging();

            /**
             @brief runtime default log level

             Defaults set by compiler define(s):
             HCELOGLEVEL
             */
            int loglevel; 

            /// return the process-wide config
            static inline const logging& get() { return logging::global_; }

        private:
            static logging global_;
            friend config_init;
        };

        /**
         Providing custom info implementations allows the user to customize the 
         thread local memory caches.
         */
        struct memory {
            memory();

            /**
             @brief function pointer to process-wide memory cache index function 

             This is defaulted to a valid implementation.

             This function is used for indexing block sizes for all caches. 
             Therefore the implementation of this function determines how block 
             sizes are distributed amongst cache buckets.
             */
            hce::config::memory::cache::info::indexer_function indexer;

            /**
             @brief pointer to object describing the system thread memory cache 

             This is defaulted to a valid implementation.

             Defaults set by compiler define(s):
             HCEMEMORYCACHEBUCKETCOUNT 
             HCEMEMORYCACHEINDEXTYPE
             HCEMEMORYCACHESYSTEMBUCKETBYTELIMIT
             */
            hce::config::memory::cache::info* system; 

            /**
             @brief pointer to object describing the global scheduler memory cache

             This is defaulted to a valid implementation.

             Defaults set by compiler define(s):
             HCEMEMORYCACHEBUCKETCOUNT 
             HCEMEMORYCACHEINDEXTYPE
             HCEMEMORYCACHEGLOBALBUCKETBYTELIMIT
             */
            hce::config::memory::cache::info* global; 

            /**
             @brief pointer to object describing the default scheduler memory cache

             This is defaulted to a valid implementation.

             Defaults set by compiler define(s):
             HCEMEMORYCACHEBUCKETCOUNT 
             HCEMEMORYCACHEINDEXTYPE
             HCEMEMORYCACHESCHEDULERBUCKETBYTELIMIT
             */
            hce::config::memory::cache::info* scheduler; 

            /// return the process-wide config
            static inline const memory& get() { return memory::global_; }

        private:
            static memory global_;
            friend hce::config::scheduler::config;
            friend hce::lifecycle::config::scheduler;
            friend hce::lifecycle::config::threadpool;
            friend config_init;
        };

        struct allocator {
            allocator();

            /**
             @brief pool_allocator's default block limit 

             This value sets the default limit for pool allocator's reusable 
             cached memory count.

             Defaults set by compiler define(s):
             HCEPOOLALLOCATORDEFAULTBLOCKLIMIT
             */
            size_t pool_allocator_default_block_limit; 

            /// return the process-wide config
            static inline const allocator& get() { return allocator::global_; }

        private:
            static allocator global_;
            friend config_init;
        };

        struct scheduler {
            scheduler(const memory&);

            /**
             @brief global scheduler config

             This is defaulted to a valid implementation.

             Defaults set by compiler define(s):
             HCEGLOBALSCHEDULERCOROUTINERESOURCELIMIT
             */
            hce::config::scheduler::config global_config; 

            /// return the process-wide config
            static inline const scheduler& get() { return scheduler::global_; }

        private:
            static scheduler global_;
            friend config_init;
        };

        struct threadpool {
            threadpool(const memory&);

            /**
             @brief count of worker schedulers in the threadpool 

             0: attempt to match scheduler count to the count of runtime detected CPU cores
             n: launch n-1 additional schedulers (the first index is always the global scheduler)

             The actual count of schedulers in the threadpool is guaranteed to be >=1.

             Defaults set by compiler define(s):
             HCETHREADPOOLSCHEDULERCOUNT
             */
            size_t count; 

            /**
             @brief threadpool scheduler config

             This is defaulted to a valid implementation.

             Defaults set by compiler define(s):
             HCETHREADPOOLCOROUTINERESOURCELIMIT
             */
            hce::config::scheduler::config worker_config; 

            /**
             @brief threadpool worker selector algorithm

             This is defaulted to a valid implementation if none is provided.
             */
            hce::config::threadpool::algorithm_function_ptr algorithm; 

            /// return the process-wide config
            static inline const threadpool& get() { return threadpool::global_; }

        private:
            static threadpool global_;
            friend config_init;
        };

        struct blocking {
            blocking();

            /**
             @brief reusable block workers count shared by the process

             Defaults set by compiler define(s):
             HCEPROCESSREUSABLEBLOCKWORKERPROCESSLIMIT
             */
            size_t reusable_block_worker_cache_size;

            /// return the process-wide config
            static inline const blocking& get() { return blocking::global_; }

        private:
            static blocking global_;
            friend config_init;
        };

        struct timer {
            timer();

            int priority;

            /**
             @brief threshold used by default timeout algorithm for when to busy-wait

             If the difference between the current time and the nearest timer 
             timeout is under this threshold, the timer thread will busy-wait 
             until timeout for increased precision.

             Defaults set by compiler define(s):
             HCETIMERBUSYWAITMICROSECONDTHRESHOLD
             */
            hce::chrono::duration busy_wait_threshold;

            /**
             @brief threshold used by default timeout algorithm for the early-wakeup duration

             How this value is used is determined by the timeout algorithm.

             Defaults set by compiler define(s):
             HCETIMEREARLYWAKEUPMICROSECONDTHRESHOLD
             */
            hce::chrono::duration early_wakeup_threshold;

            /**
             @brief threshold used by default timeout algorithm for when to begin early-wakeups for long running timers

             How this value is used is determined by the timeout algorithm.

             Defaults set by compiler define(s):
             HCETIMEREARLYWAKEUPMICROSECONDLONGTHRESHOLD
             */
            hce::chrono::duration early_wakeup_long_threshold;

            /**
             @brief timer service timeout algorithm

             Algorithm for calculating early timeouts for the timer service.
             The timer service will override the result of this algorithm if 
             the returned timeout exceeds the timeout of the nearest timer 
             timeout.

             This is defaulted to a valid implementation if none is provided.
             */
            hce::config::timer::algorithm_function_ptr algorithm;

            /// return the process-wide config
            static inline const timer& get() { return timer::global_; }

        private:
            static timer global_;
            friend config_init;
        };

        config() : sch(mem), tp(mem) {}

        logging log;
        memory mem;
        allocator alloc;
        scheduler sch;
        threadpool tp;
        blocking blk;
        timer tmr;
    };

    virtual ~lifecycle() { 
        HCE_INFO_DESTRUCTOR(); 
    }

    /**
     @brief set the hce framework's global configuration and allocate, construct and start the framework 

     Argument `hce::lifecycle::config` configures the process-wide framework 
     configuration, which is utilized by all `hce::config::` namespace 
     operations. Access to the process-wide configuration is lockless.

     The returned lifecycle object starts the hce framework and manages its 
     memory. When the returned lifecycle object goes out scope the hce framework 
     will be shutdown and destroyed. 

     All other features rely on this object staying in existence. Therefore, all 
     launched operations (*INCLUDING* all `hce::memory::` based deallocations) 
     must complete and join before this object can safely go out of existence. 
     Failure to do this can cause memory exceptions or deadlock.

     There can only be one lifecycle in existence at a time. If this is called 
     again while the first one still exists, the process will exit. It is also 
     not recommended to continually create and destroy lifecycles, instead to 
     keep one in existence until process exit. Recommended design is to hold 
     this pointer on the `main()` thread's stack at a point before any `hce::`
     operations are launched.

     @param c optional framework configuration
     @return a lifecycle object managing the memory of the hce framework
     */
    static inline std::unique_ptr<hce::lifecycle> initialize(config c = {}) {
        return std::unique_ptr<hce::lifecycle>(new lifecycle(c));
    }

    /*
     It is a process critical error if this is called and `hce::initialize()` 
     has either never been called or the returned lifecycle has gone out of 
     scope. If this occurs, the process will exit.

     @return the process wide lifecycle
     */
    static inline hce::lifecycle& get() { return *(lifecycle::instance_); }

    static inline std::string info_name() { return "hce::lifecycle"; }
    inline std::string name() const { return lifecycle::info_name(); }

private:
    /*
     RAII object for constructing and erasing `thread_local` memory caches. 
     Instances of this object are handled by `hce::memory::cache::get()`, which 
     is why `hce::memory::cache` is a `friend`.
     */
    struct scoped_cache {
        scoped_cache() : 
            key_(std::this_thread::get_id()),
            // allocate the memory cache with the configured info
            cache_(
                new hce::memory::cache(
                    hce::config::memory::cache::info::get()))
        { 

            // stash the allocated pointer in case of exception
            std::unique_ptr<hce::memory::cache> cache(cache_);

            // acquire the process-wide lifecycle lock
            std::lock_guard<hce::spinlock> lk(lifecycle::slk_);

            if(hce::lifecycle::instance_) [[likely]] {
                hce::lifecycle::instance_->insert_cache_(key_, std::move(cache));
            } else [[unlikely]] {
                throw cannot_register_cache_without_lifecycle();
            }
        }

        ~scoped_cache() {
            // acquire the process-wide lifecycle lock
            std::lock_guard<hce::spinlock> lk(lifecycle::slk_);

            /*
             It is not an error if the lifecycle pointer is not set, it is 
             possible for a thread to go out of scope after the lifecycle 
             instance does (IE, the main thread). If that scenario occurs, the 
             cache has already been destroyed and it is safe to continue.
             */
            if(hce::lifecycle::instance_) [[likely]] {
                hce::lifecycle::instance_->erase_cache_(key_);
            }
        }

        inline hce::memory::cache& cache() { return *cache_; }

    private:
        const std::thread::id key_;
        hce::memory::cache* cache_;
    };

    struct self_init {
        // sets the global instance pointer.
        self_init(lifecycle* self) {
            // acquire the global lock during initialization and destruction
            std::lock_guard<hce::spinlock> lk(lifecycle::slk_);

            if(lifecycle::instance_) [[unlikely]] {
                HCE_FATAL_FUNCTION_BODY("hce::lifecyle::initialize()","hce::lifecycle already exists, cannot proceed. Process exiting...");
                std::terminate();
            } else [[likely]] {
                lifecycle::instance_ = self;
            }
        }

        ~self_init() {
            std::lock_guard<hce::spinlock> lk(lifecycle::slk_);
            lifecycle::instance_ = nullptr;
        }
    };

    // initialize config with RAII
    struct config_init {
        config_init(const config& c) {
            // overwrite global configurations with provided configurations
            lifecycle::config::logging::global_ = c.log;
            lifecycle::config::memory::global_ = c.mem;
            lifecycle::config::allocator::global_ = c.alloc;
            lifecycle::config::scheduler::global_ = c.sch;
            lifecycle::config::threadpool::global_ = c.tp;
            lifecycle::config::blocking::global_ = c.blk;
            lifecycle::config::timer::global_ = c.tmr;
        }
    };

    // initialize logging with RAII
    struct logging_init {
        logging_init();
    };

    lifecycle(const config& c) :
        self_init_(this), 
        config_init_(c),
        logging_init_()
    {
        HCE_INFO_CONSTRUCTOR();
    }

    inline void insert_cache_(const std::thread::id& key,
                              std::unique_ptr<hce::memory::cache>&& cache) 
    {
        auto it = memory_caches_.find(key);

        if(it != memory_caches_.end()) [[unlikely]] {
            /*
             This a guard against unintended design changes. The expected design 
             is that each thread will create and maintain at most one 
             hce::memory::cache via a `scoped_cache` instance.
             */
            throw cache_already_registered(key);
        }
           
        memory_caches_[key] = std::move(cache);
    }
    
    inline void erase_cache_(const std::thread::id& key) {
        auto it = memory_caches_.find(key);

        if(it != memory_caches_.end()) [[likely]] {
            memory_caches_.erase(it);
        }
    }

    static hce::spinlock slk_; // used to synchronize access to instance_
    static lifecycle* instance_; // the pointer to the global lifecycle instance  

    // set the process-wide lifecycle pointer
    self_init self_init_;

    // overwrite the global config with the given config
    config_init config_init_;
    
    // initialize logging for this library
    logging_init logging_init_;

    /*
     The map of memory caches. These are constructed and erased dynamically by 
     `thread_local` instances of `scoped_cache` in `hce::memory::cache::get()` 
     calls. When an `hce::memory::cache` is destroyed all memory it was caching 
     is also freed.

     It is important that these caches be destroyed after all dependencies but 
     before process exit, hence their management here. 
     */
    std::map<std::thread::id,std::unique_ptr<hce::memory::cache>> memory_caches_;

    // the various services, in order of dependencies
    hce::scheduler::lifecycle::service scheduler_lifecycle_service_;
    hce::scheduler::global::service scheduler_global_service_;
    hce::threadpool::service threadpool_service_;
    hce::blocking::service blocking_service_;
    hce::timer::service timer_service_;

    // needs to acquire `thread_local` `scoped_cache` instances.
    friend hce::memory::cache;
};

/**
 @brief a convenience for calling `hce::lifecycle::initialize()`
 @returns the newly allocated and constructed process `hce::lifecycle` pointer
 */
inline std::unique_ptr<lifecycle> initialize() {
    return hce::lifecycle::initialize();
}

}

#endif
