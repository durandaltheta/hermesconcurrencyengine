//SPDX-License-Identifier: Apache-2.0
//Author: Blayne Dennis 
#include <thread>
#include <memory>

#include "coroutine.hpp"

// test, only uncomment for development of this library
//#include "dev_print.hpp"

hce::coroutine*& hce::detail::tl_this_coroutine() {
    thread_local hce::coroutine* tltc = nullptr;
    return tltc;
}

hce::this_thread* hce::this_thread::get() {
    thread_local hce::this_thread tlatt;
    return &tlatt;
}
