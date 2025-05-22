//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#include <memory>
#include <unordered_set>

#include "utility.hpp"
#include "thread.hpp"
#include "scheduler.hpp"
#include "thread_key_map.hpp"

hce::scheduler*& hce::detail::scheduler::tl_this_scheduler() {
    thread_local hce::thread::local::ptr<hce::thread::key::scheduler> p;
    return p.ref();
}

std::unique_ptr<hce::list<std::coroutine_handle<>>>*& 
hce::detail::scheduler::tl_this_scheduler_local_queue() {
    thread_local hce::thread::local::ptr<hce::thread::key::scheduler_local_queue> p;
    return p.ref();
}
