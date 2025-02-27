//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#include "logging.hpp"
#include "thread.hpp"
#include "thread_key_map.hpp"

int& hce::logger::tl_loglevel() {
    struct init {
        init() : i(-9) { 
            if(level.ref() == nullptr) {
                i = hce::config::logging::default_log_level();
                level.ref() = &i;
            }
        }

        int i;
        hce::thread::local::ptr<hce::thread::key::loglevel> level;
    };

    thread_local init i;
    return *(i.level.ref());
}

/// return the thread local loglevel
int hce::logger::thread_log_level() { return hce::logger::tl_loglevel(); }

/// set the thread local loglevel
void hce::logger::thread_log_level(int level) {
    if(level > 9) { level = 9; }
    else if(level < -9) { level = -9; }
    hce::logger::tl_loglevel() = level;
}
