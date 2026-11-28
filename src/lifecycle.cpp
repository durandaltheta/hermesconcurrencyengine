#include <sstream>

#include "hcec/hcec.h"
#include "hce/logging.hpp"
#include "hce/lifecycle.hpp"

#ifndef HCELOGLEVEL
/*
 Library compile time macro determining default printing log level. Default to 
 loguru::Verbosity_WARNING.
 */
#define HCELOGLEVEL -1
#endif 

// force a loglevel of loguru::Verbosity_OFF or higher
#if HCELOGLEVEL < -10 
#define HCELOGLEVEL loguru::Verbosity_OFF
#endif 

// force a loglevel of 9 or lower
#if HCELOGLEVEL > 9
#define HCELOGLEVEL 9
#endif 

// called once at process start
struct hce_log_initializer {
    hce_log_initializer() {
        int loglevel = HCELOGLEVEL;
        std::stringstream ss;
        ss << "-v" << loglevel;
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
} g_hce_log_initializer; 


hce::lifecycle::config::timer::timer() :
{ }

hce::lifecycle::config::network::network() :
    thread_count(1)
{ }

struct erased_impl : hce::lifecycle::erased {
    erased_impl(std::unique_ptr<hce::thread::local>&& l) : local_(std::move(l)) { 
    }

    ~erased_impl() { }

    // the various services, in order of dependencies
    std::unique_ptr<hce::thread::local> local_;
    std::unique_ptr<hce::owner<hce::scheduler::lifecycle::manager>> scheduler_lifecycle_manager_;
    std::unique_ptr<hce::owner<hce::scheduler::global>> scheduler_global_;
    std::unique_ptr<hce::owner<hce::threadpool>> threadpool_;
    std::unique_ptr<hce::owner<hce::blocking>> blocking_;
    std::unique_ptr<hce::owner<hce::timer>> timer_;
    std::unique_ptr<hce::owner<hce::network>> network_;
};

hce::lifecycle::erased::~erased(){ }

void hce::lifecycle::erased::deleter::operator()(hce::lifecycle::erased* e) const {
    if(e) {
        delete (erased_impl*)e;
    }
}

std::unique_ptr<hce::lifecycle> hce::lifecycle::initialize(hce::lifecycle::config c) {
    return std::unique_ptr<hce::lifecycle>(
        new lifecycle(
            c, 
            // initialize thread locals first
            std::unique_ptr<hce::thread::local>(new hce::thread::local)));
}

hce::lifecycle::~lifecycle() { HCE_INFO_DESTRUCTOR(); }

std::string hce::lifecycle::info_name() { return "hce::lifecycle"; }
std::string hce::lifecycle::name() const { return lifecycle::info_name(); }

/// return the lifecycle's config
const hce::lifecycle::config& hce::lifecycle::get_config() { return config_; }

// initialize hce::thread::local before lifecycle so logging can function
hce::lifecycle::lifecycle(const config& c, std::unique_ptr<hce::thread::local>&& l) : 
    config_(c),
    erased_((erased*)new erased_impl(std::move(l)))
{ 
    // allocate all the owner<> instances
    auto& ei = *((erased_impl*)(erased_.get()));
    ei.scheduler_lifecycle_manager_.reset(new hce::owner<hce::scheduler::lifecycle::manager>);
    ei.scheduler_global_.reset(new hce::owner<hce::scheduler::global>);
    ei.threadpool_.reset(new hce::owner<hce::threadpool>);
    ei.blocking_.reset(new hce::owner<hce::blocking>);
    ei.timer_.reset(new hce::owner<hce::timer>);
    ei.network_.reset(new hce::owner<hce::network>);

    HCE_INFO_CONSTRUCTOR(); 
}
