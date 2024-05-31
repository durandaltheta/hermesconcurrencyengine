//SPDX-License-Identifier: Apache-2.0
//Author: Blayne Dennis 
#include <thread>
#include <memory>

#include "loguru.hpp"
#include "coroutine.hpp"
#include "scheduler.hpp"

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

void hce::detail::coroutine_did_not_co_await(void* awt) {
    std::stringstream ss;
    ss << "hce::base_coroutine[0x" 
       << (void*)&(base_coroutine::local())
       << "] did not call co_await on an hce::base_awaitable[0x"
       << awt 
       << "]";
    LOG_F(ERROR, ss.str().c_str());
}

void hce::detail::awaitable_not_resumed(void* awt, void* hdl) {
    std::stringstream ss;
    ss << "hce::base_awaitable[0x" 
       << awt
       << "] was not resumed before being destroyed; it held std::coroutine_handle<>::address()[0x"
       << hdl 
       << "]";
    LOG_F(ERROR, ss.str().c_str());
}
