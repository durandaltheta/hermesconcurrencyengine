#include "hcec/hcec_scheduler.h"
#include "hcec/hcec.h"
#include "hce/scheduler.hpp"

void hce_scheduler_config_initialize(hce_scheduler_config_t* cfg) {
    if(!cfg) { return; }
    auto& c = *cfg;

    // scheduler
    {
        c.sch.loglevel = hce_default_log_level();
        c.sch.reusable_coroutine_handle_limit = 
            HCEREUSABLECOROUTINEHANDLEDEFAULTSCHEDULERLIMIT;
    }
}

hce_scheduler_lifecycle_t* hce_scheduler_make(hce_scheduler_config_t* cfg) {
    return cfg 
        ? hce::scheduler::make(*((hce::config::scheduler::config*)cfg)).release();
        : hce::scheduler::make().release();
}

void hce_scheduler_destroy(hce_scheduler_lifecycle_t* lifecycle) {
    std::unique_ptr<hce::scheduler> lf((hce::scheduler::lifecycle*)lifecycle);
}
