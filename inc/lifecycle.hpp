//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#ifndef HERMES_COROUTINE_ENGINE_LIFECYCLE
#define HERMES_COROUTINE_ENGINE_LIFECYCLE

#include <memory>
#include <string>
#include <sstream>
#include <exception>

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
 @brief RAII management object for this framework

 The instance of this object constructs, allocations and maintains all 
 process-wide singleton services and objects.
 */
struct lifecycle : public printable {
    /**
     Configuration object for the framework 

     This object can be passed to hce::lifecycle::initialize() to configure the 
     framework at runtime. Default values are determined by compiler defines.
     */
    struct config {
        struct logging {
            logging();

            int loglevel; //< runtime log level
        };

        /**
         Providing custom info implementations allows the user to customize the 
         thread local memory caches.
         */
        struct memory {
            memory();

            hce::config::memory::cache::info* system; //< pointer to object describing the system cache
            hce::config::memory::cache::info* global; //< pointer to object describing the global scheduler cache
            hce::config::memory::cache::info* scheduler; //< pointer to object describing the default scheduler cache 
            size_t pool_allocator_default_block_limit; //< pool_allocator's default block limit
        };

        struct scheduler {
            scheduler();

            size_t default_resource_limit;
            hce::scheduler::config global; //< global scheduler config
            hce::scheduler::config standard; //< default scheduler config
        };

        struct threadpool {
            threadpool();

            size_t count; //< count of worker schedulers in the threadpool
            hce::scheduler::config config; //< worker scheduler config 
            hce::config::threadpool::algorithm_function_ptr algorithm; //< worker selection function ptr
        };

        struct blocking {
            blocking();

            size_t process_worker_resource_limit; //< reusable block workers count shared by the process
            size_t global_scheduler_worker_resource_limit; //< reusable block workers count for the global scheduler
            size_t default_scheduler_worker_resource_limit; //< reusable block workers count for default scheduers
        };

        struct timer {
            timer();

            hce::chrono::duration busy_wait_threshold;
            hce::chrono::duration early_wakeup_threshold;
            hce::chrono::duration early_wakeup_long_threshold;
            hce::timer::service::algorithm_function_ptr algorithm;
        };

        logging log;
        memory mem;
        scheduler sch;
        threadpool tp;
        blocking blk;
        timer tmr;
    };

    ~lifecycle() { HCE_INFO_DESTRUCTOR(); }

    /**
     @brief allocate, construct and start the hce framework 

     The returned lifecycle object starts the hce framework and manages its memory.
     When the returned lifecycle object goes out scope the hce framework will be 
     shutdown and destroyed. 

     All other features rely on this object staying in existence. All launched
     operations must complete and join before this object can safely go out of 
     existence.

     There can only be one lifecycle in existence at a time. If this is called 
     again while the first one still exists, the process will exit.

     @return a lifecycle object managing the memory of the hce framework
     */
    static inline std::unique_ptr<hce::lifecycle> initialize(config c) {
        if(lifecycle::instance_) [[unlikely]] {
            HCE_FATAL_FUNCTION_BODY("hce::lifecyle::initialize()","lifecycle already exists, cannot proceed. Process exiting...");
            std::terminate();
        } else [[likely]] {
            return std::unique_ptr<hce::lifecycle>(new lifecycle(c));
        }
    }
   
    /**
     @brief allocate, construct and start the hce framework with default values
     */
    static inline std::unique_ptr<hce::lifecycle> initialize() {
        config c;
        return initialize(c);
    }

    /*
     It is a process critical error if this is called and `hce::initialize()` 
     has either never been called or the returned lifecycle has gone out of 
     scope. If this occurs, the process will exit.

     @return the process wide lifecycle
     */
    static inline hce::lifecycle& get() {
        if(lifecycle::instance_) [[likely]] {
            return *(lifecycle::instance_);
        } else {
            HCE_FATAL_FUNCTION_BODY("hce::lifecyle::get()", "lifecycle does not exist, either hce::lifecycle::initialize() was not called or returned hce::lifecycle pointer has been destroyed. Process exiting...");
            std::terminate();
        }
    }

    static inline std::string info_name() { return "hce::lifecycle"; }
    inline std::string name() const { return lifecycle::info_name(); }

    /// return the hce framework's configuration
    inline const config& configuration() const { return config_; }

private:
    // RAII object for setting and unsetting references to pointers
    template <typename T>
    struct scoped_ptr_ref {
        scoped_ptr_ref(T*& tref, T* value) : tref_(tref), old_(tref_) {
            // set the new value
            tref_ = value;
        }

        ~scoped_ptr_ref() {
            // reset to the old value
            tref_ = old_;
        }

    private:
        T*& tref_; // the pointer reference
        T* old_; // the cached original pointer value
    };

    // use the configuration to initialize logging
    struct logging_init {
        logging_init(const config& c) {
            std::stringstream ss;
            ss << "-v" << c.log.loglevel;
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

    // use the configuration to initialize memory
    struct memory_init {
        memory_init(const config& c);
    };

    lifecycle(config c) :
        logging_init_(c),
        memory_init_(c),
        config_(c),
        sc_self_(hce::lifecycle::instance_, this),
        sc_main_cache_(hce::memory::cache::main_cache_, &main_cache_),
        sc_manager_(hce::scheduler::lifecycle::manager::instance_, &manager_),
        sc_global_(
            hce::scheduler::global_, 
            [&]() -> hce::scheduler* {
                // set the global request flag before constructing the global scheduler
                auto lf = hce::scheduler::make_(config_.sch.global, true);
                hce::scheduler* sch = &(lf->scheduler());
                manager_.registration(std::move(lf));
                return sch;
            }()),
        sc_threadpool_(hce::threadpool::instance_, &threadpool_),
        sc_blocking_service_(hce::blocking::service::instance_, &blocking_service_),
        sc_timer_service_(hce::timer::service::instance_, &timer_service_)
    {
        HCE_INFO_CONSTRUCTOR();
    }

    static hce::lifecycle* instance_;

    static hce::chrono::time_point timeout_algorithm(
        const hce::chrono::time_point& now, 
        const hce::chrono::time_point& requested_timeout);

    logging_init logging_init_;
    memory_init memory_init_;

    config config_;
    scoped_ptr_ref<hce::lifecycle> sc_self_;

    hce::memory::cache main_cache_;
    scoped_ptr_ref<hce::memory::cache> sc_main_cache_;

    hce::scheduler::lifecycle::manager manager_;
    scoped_ptr_ref<hce::scheduler::lifecycle::manager> sc_manager_;

    scoped_ptr_ref<hce::scheduler> sc_global_;

    hce::threadpool threadpool_;
    scoped_ptr_ref<hce::threadpool> sc_threadpool_;

    hce::blocking::service blocking_service_;
    scoped_ptr_ref<hce::blocking::service> sc_blocking_service_;

    hce::timer::service timer_service_;
    scoped_ptr_ref<hce::timer::service> sc_timer_service_;
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
