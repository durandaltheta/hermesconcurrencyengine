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

hce::detail::coroutine::this_thread* 
hce::detail::coroutine::this_thread::get() {
    thread_local hce::detail::coroutine::this_thread tlatt;
    return &tlatt;
}
