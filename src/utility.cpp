#include <cstring>
#include <vector>

#include "loguru.hpp"
#include "utility.hpp"

/// library compile time macro determining default printing log level
#ifndef HCELOGLEVEL
// default to under loguru::Verbosity_INFO
#define HCELOGLEVEL -1
#endif 

/// force a loglevel of loguru::Verbosity_OFF or higher
#if HCELOGLEVEL < -10 
#define HCELOGLEVEL loguru::Verbosity_OFF
#endif 

/// force a loglevel of 9 or lower
#if HCELOGLEVEL > 9
#define HCELOGLEVEL 9
#endif

/// declaration of user replacable log initialization function
extern void hce_log_initialize(int hceloglevel);

#ifndef HCECUSTOMLOGINIT
/// the default log initialization code is defined here
void hce_log_initialize(int hceloglevel) {
    std::stringstream ss;
    ss << "-v" << hceloglevel;
    std::string process("hermesconcurrencyengine");
    std::string verbosity = ss.str();
    std::vector<char*> argv;
    argv.push_back(process.data());
    argv.push_back(verbosity.data());
    int argc=argv.size();
    loguru::Options opt;
    opt.main_thread_name = nullptr;
    opt.signal_options = loguru::SignalOptions::none();
    loguru::init(argc, argv.data(), opt);
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
