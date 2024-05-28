//SPDX-License-Identifier: Apache-2.0
//Author: Blayne Dennis 
#include <thread>
#include <memory>

#include "coroutine.hpp"

// test, only uncomment for development of this library
//#include "dev_print.hpp"

hce::base_coroutine*& hce::base_coroutine::tl_this_coroutine() {
    thread_local hce::base_coroutine* tltc = nullptr;
    return tltc;
}

hce::this_thread* hce::this_thread::get() {
    thread_local hce::this_thread tlatt;
    return &tlatt;
}
