#include <cstring>
#include <vector>

#include "loguru.hpp"
#include "utility.hpp"

#ifndef HCELOGLEVEL
/// library compile time macro determining default printing log level
#define HCELOGLEVEL 0
#endif 

#if HCELOGLEVEL > 9
/// force a loglevel of 9 or lower
#define HCELOGLEVEL 9
#endif

#if HCELOGLEVEL < 0
/// force a loglevel of 0 or higher
#define HCELOGLEVEL 0
#endif 

/// declaration of user replacable log initialization function
extern void hce_log_initialize(int hceloglevel);

#ifndef HCECUSTOMLOGINIT
/// the default log initialization code is defined here
void hce_log_initialize(int hceloglevel) {
    std::stringstream ss;
    ss << "-v" << HCELOGLEVEL;
    std::string process("hermesconcurrencyengine");
    std::string verbosity = ss.str();
    std::vector<char*> argv;
    argv.push_back(process.data());
    argv.push_back(verbosity.data());
    int argc=argv.size();
    loguru::init(argc, argv.data());
}
#endif 

namespace hce {
namespace detail {

struct log_initializer {
    // responsible for initializing the loguru framework
    log_initializer() { hce_log_initialize(HCELOGLEVEL); }
    int loglevel() const { return loguru::current_verbosity_cutoff(); }
} g_log_initializer;

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
    hce::printable::tl_loglevel() = level;
}
