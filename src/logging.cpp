//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#include "logging.hpp"
#include "thread.hpp"
    
static thread_local int tl_hce_log_level = hce::config::logging::default_log_level();

/// return the thread local loglevel
int hce::logger::thread_log_level() { 
    return tl_hce_log_level;
}

/// set the thread local loglevel
void hce::logger::thread_log_level(int level) {
    if(level > 9) { level = 9; }
    else if(level < -9) { level = -9; }
    tl_hce_log_level = level;
}
