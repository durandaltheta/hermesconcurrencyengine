//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#include "logging.hpp"

int hce::logger::default_log_level() { 
    return loguru::current_verbosity_cutoff(); 
}

int& hce::logger::tl_loglevel() {
    thread_local int level = hce::logger::default_log_level();
    return level;
}

/// return the thread local loglevel
int hce::logger::thread_log_level() { return hce::logger::tl_loglevel(); }

/// set the thread local loglevel
void hce::logger::thread_log_level(int level) {
    if(level > 9) { level = 9; }
    else if(level < -9) { level = -9; }
    hce::logger::tl_loglevel() = level;
}
