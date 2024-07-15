//SPDX-License-Identifier: Apache-2.0
//Author: Blayne Dennis 
#include <string>
#include <sstream>

#include "loguru.hpp"
#include "utility.hpp"
#include "coroutine.hpp"

// test, only uncomment for development of this library
//#include "dev_print.hpp"

hce::coroutine*& hce::detail::coroutine::tl_this_coroutine() {
    thread_local hce::coroutine* tltc = nullptr;
    return tltc;
}

hce::detail::coroutine::this_thread* 
hce::detail::coroutine::this_thread::get() {
    thread_local hce::detail::coroutine::this_thread tlatt;
    return &tlatt;
}

void hce::detail::coroutine::coroutine_did_not_co_await(void* awt) {
    std::stringstream ss;
    ss << "hce::coroutine[0x" 
       << (void*)(hce::coroutine::local().address())
       << "] did not call co_await on an hce::awaitable[0x"
       << awt 
       << "]";
    HCE_ERROR_LOG("%s",ss.str().c_str());
}
