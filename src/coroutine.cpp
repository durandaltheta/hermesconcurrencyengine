//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#include <string>
#include <sstream>
#include <thread>

#include "loguru.hpp"
#include "atomic.hpp"
#include "coroutine.hpp"

hce::coroutine*& hce::detail::coroutine::tl_this_coroutine() {
    thread_local hce::coroutine* tltc = nullptr;
    return tltc;
}

size_t& hce::coroutine::promise_type::stashed_alloc_size() {
    thread_local size_t alloc_size = 0;
    return alloc_size;
}

size_t& hce::awaitable::interface::stashed_alloc_size() {
    thread_local size_t alloc_size = 0;
    return alloc_size;
}

hce::detail::coroutine::this_thread* 
hce::detail::coroutine::this_thread::get() {
    thread_local hce::detail::coroutine::this_thread tlatt;
    return &tlatt;
}
