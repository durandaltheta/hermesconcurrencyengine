//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#include <memory>
#include <unordered_set>

#include "utility.hpp"
#include "memory.hpp"
#include "thread.hpp"
#include "scheduler.hpp"
#include "channel.hpp"
#include "lifecycle.hpp"

hce::scheduler::lifecycle::service* hce::scheduler::lifecycle::service::instance_ = nullptr;
hce::scheduler::global::service* hce::scheduler::global::service::instance_ = nullptr;

hce::scheduler*& hce::detail::scheduler::tl_this_scheduler() {
    thread_local hce::scheduler* tlts = nullptr;
    return tlts;
}

std::unique_ptr<hce::list<std::coroutine_handle<>>>*& 
hce::detail::scheduler::tl_this_scheduler_local_queue() {
    thread_local std::unique_ptr<hce::list<std::coroutine_handle<>>>* tllq = nullptr;
    return tllq;
}
