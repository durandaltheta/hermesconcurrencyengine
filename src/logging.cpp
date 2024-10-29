//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#include "loguru.hpp"
#include "logging.hpp"

namespace hce {
namespace detail {

struct log_initializer {
    // responsible for initializing the loguru framework
    log_initializer() { hce::config::initialize_log(); }
    int loglevel() const { return loguru::current_verbosity_cutoff(); }
} g_log_initializer; // globals are initialized before entering main()

}
}

int hce::printable::default_log_level() { 
    return hce::detail::g_log_initializer.loglevel(); 
}

int& hce::printable::tl_loglevel() {
    thread_local int level = hce::printable::default_log_level();
    return level;
}

/// return the thread local loglevel
int hce::printable::thread_log_level() { return hce::printable::tl_loglevel(); }

/// set the thread local loglevel
void hce::printable::thread_log_level(int level) {
    if(level > 9) { level = 9; }
    else if(level < -9) { level = -9; }
    hce::printable::tl_loglevel() = level;
}
