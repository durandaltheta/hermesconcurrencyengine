//SPDX-License-Identifier: Apache-2.0
//Author: Blayne Dennis 
#include <thread>
#include <memory>
#include <mutex>
#include <deque>

#include "atomic.hpp"
#include "scheduler.hpp"

hce::scheduler*& hce::detail::scheduler::tl_this_scheduler() {
    thread_local hce::scheduler* tlts = nullptr;
    return tlts;
}

hce::scheduler*& hce::detail::scheduler::tl_this_scheduler_redirect() {
    thread_local hce::scheduler* tlts = nullptr;
    return tlts;
}

hce::scheduler::lifecycle::manager& hce::scheduler::lifecycle::manager::instance() {  
    static hce::scheduler::lifecycle::manager m;
    return m;
}

hce::scheduler& hce::scheduler::global_() {
    static std::shared_ptr<hce::scheduler> sch(
        []() -> std::shared_ptr<hce::scheduler> {
            auto sch = hce::scheduler::make();
            std::thread([=]() mutable { sch->install(); }).detach();
            return sch;
        }());
    return *sch;
}
