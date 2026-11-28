#pragma once
//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 

#include <cstddef>
#include <memory>
#include <string>

#include "hcec/hcec.h"
#include "hce/printable.hpp"
#include "hce/scheduler/config.hpp"
#include "hce/threadpool/config.hpp"

namespace hce {

/**
 @brief RAII configuration and management object for this framework

 The instance of this object configures the framework and constructs, allocates 
 and maintains all singleton services.
 */
struct lifecycle : service<lifecycle>, hce::printable, hce_lifecycle_t {
    /**
     @brief configuration for the framework 

     The user can customize these options at runtime and pass the result to 
     `hce::lifecycle::initialize()` to set the process-wide configuration.
     Reads to process-wide configuration values are lockless.

     Default values are determined by compiler defines (see below). 
     */
    struct config : hce_lifecycle_config_t {
        config();

        using logging = hce_lifecycle_config_logging_t;
        using allocator = hce_lifecycle_allocator_config_t;
        using scheduler = hce_lifecycle_scheduler_config_t;
        using threadpool = hce_threadpool_config_t;
        using blocking = hce_lifecycle_blocking_config_t;
        using timer = hce_lifecycle_timer_config_t;
        using network = hce_lifecycle_network_config_t;
    };

    // don't expose all the includes to the users of this core header
    struct erased {
        struct deleter {
            void operator()(erased* e) const;
        };

        virtual ~erased() = 0;
    };

    virtual ~lifecycle();

    /**
     @brief set the hce framework's global configuration and allocate, construct and start the framework 

     Argument `hce::lifecycle::config` configures the process-wide framework 
     configuration, which is utilized by all `hce::config::` namespace 
     operations. Access to the process-wide configuration is lockless.

     The returned lifecycle object starts the hce framework and manages its 
     memory. When the returned lifecycle object goes out scope the hce framework 
     will be shutdown and destroyed. 

     All other features rely on this object staying in existence. Therefore, all 
     launched operations must complete and join before this object can safely go 
     out of existence. Failure to do this can cause memory exceptions or 
     deadlock.

     There can only be one lifecycle in existence at a time. If this is called 
     again while the first one still exists, the process will exit. It is also 
     not recommended to continually create and destroy lifecycles, instead to 
     keep one in existence until process exit. Recommended design is to hold 
     this pointer on the `main()` thread's stack at a point before any `hce::`
     operations are launched.

     @param c optional framework configuration
     @return a lifecycle object managing the memory of the hce framework
     */
    static std::unique_ptr<hce::lifecycle> initialize(config c = {});

    static std::string info_name();
    std::string name() const;

    /// return the lifecycle's config
    const config& get_config();

private:
    // initialize hce::thread::local before lifecycle so logging can function
    lifecycle(const config& c, std::unique_ptr<hce::thread::local>&& l);

    std::mutex mtx_;

    // global configuration for the framework
    config config_;

    std::unique_ptr<erased,erased::deleter> erased_;
};

/**
 @brief a convenience for calling `hce::lifecycle::initialize()`
 @returns the newly allocated and constructed process `hce::lifecycle` pointer
 */
inline std::unique_ptr<lifecycle> initialize() {
    return hce::lifecycle::initialize();
}

}
