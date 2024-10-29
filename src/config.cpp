//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#include <cstring>
#include <memory>
#include <vector>

#include "loguru.hpp"
#include "utility.hpp"
#include "scheduler.hpp"
#include "threadpool.hpp"

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

// the count of coroutines to grow to cache resources for in the global scheduler
#ifndef HCEGLOBALCOROUTINEPOOLLIMIT
#define HCEGLOBALCOROUTINEPOOLLIMIT 512
#endif 

// count of potentially cached, reusable block() worker threads for the global
// scheduler
#ifndef HCEGLOBALBLOCKWORKERPOOLLIMIT
#define HCEGLOBALBLOCKWORKERPOOLLIMIT 1
#endif 

/*
 The count of threadpool schedulers. A value greater than 1 will cause the 
 threadpool to launch count-1 schedulers (the global scheduler is always the 
 first scheduler in the threadpool). A value of 0 allows the library to decide
 the total scheduler count. 
 */
#ifndef HCETHREADPOOLSCHEDULERCOUNT
#define HCETHREADPOOLSCHEDULERCOUNT 0
#endif

// the count of coroutines to grow to cache resources for in threadpool schedulers
#ifndef HCETHREADPOOLCOROUTINEPOOLLIMIT
#define HCETHREADPOOLCOROUTINEPOOLLIMIT 512
#endif 

// count of potentially cached, reusable block() worker threads for 
// threadpool schedulers
#ifndef HCETHREADPOOLBLOCKWORKERPOOLLIMIT
#define HCETHREADPOOLBLOCKWORKERPOOLLIMIT 0
#endif 

/// the default log initialization code is defined here
void hce::config::initialize_log() {
    std::stringstream ss;
    ss << "-v" << HCELOGLEVEL;
    std::string process("hce");
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

std::unique_ptr<hce::scheduler::config> hce::config::global::scheduler_config() {
    auto config = hce::scheduler::config::make();
    config->coroutine_pool_limit = HCEGLOBALCOROUTINEPOOLLIMIT;
    config->block_worker_pool_limit = HCEGLOBALBLOCKWORKERPOOLLIMIT;
    return config;
}

std::unique_ptr<hce::scheduler::config> hce::config::threadpool::scheduler_config() {
    auto config = hce::scheduler::config::make();
    config->coroutine_pool_limit = HCETHREADPOOLCOROUTINEPOOLLIMIT;
    config->block_worker_pool_limit = HCETHREADPOOLBLOCKWORKERPOOLLIMIT;
    return config;
}

size_t hce::config::threadpool::scheduler_count() {
    return HCETHREADPOOLSCHEDULERCOUNT;
}

hce::config::threadpool::algorithm_function_ptr hce::config::threadpool::algorithm() {
    return &hce::threadpool::lightest;
}
