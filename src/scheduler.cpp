//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#include <memory>
#include <unordered_set>

#include "utility.hpp"
#include "memory.hpp"
#include "scheduler.hpp"
#include "channel.hpp"

hce::scheduler*& hce::detail::scheduler::tl_this_scheduler() {
    thread_local hce::scheduler* tlts = nullptr;
    return tlts;
}

std::unique_ptr<hce::list<std::coroutine_handle<>>>*& 
hce::detail::scheduler::tl_this_scheduler_local_queue() {
    thread_local std::unique_ptr<hce::list<std::coroutine_handle<>>>* tllq = nullptr;
    return tllq;
}

hce::scheduler::lifecycle::manager& hce::scheduler::lifecycle::manager::instance() {  
    static hce::scheduler::lifecycle::manager m;
    return m;
}

bool& hce::scheduler::blocking::worker::tl_is_worker() {
    thread_local bool is_wkr = false;
    return is_wkr;
}

void hce::scheduler::lifecycle::global_thread_function(hce::scheduler* sch) {
    hce::config::memory::cache::info::thread::get_type() = 
        hce::config::memory::cache::info::thread::type::global;
    sch->run();
}

void hce::scheduler::lifecycle::scheduler_thread_function(hce::scheduler* sch) {
    hce::config::memory::cache::info::thread::get_type() = 
        hce::config::memory::cache::info::thread::type::scheduler;
    sch->run();
}

hce::scheduler& hce::scheduler::global_() {
    static std::shared_ptr<hce::scheduler> sch(
        []() -> std::shared_ptr<hce::scheduler> {
            // set the global request flag before construction so lifecycle is 
            // notified
            auto lf = hce::scheduler::make_(hce::config::global::scheduler_config(),true);
            std::shared_ptr<hce::scheduler> scheduler = lf->scheduler();
            hce::scheduler::lifecycle::manager::instance().registration(std::move(lf));
            return scheduler;
        }());

    return *sch;
}
