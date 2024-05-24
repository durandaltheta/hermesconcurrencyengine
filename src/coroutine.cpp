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

hce::awaitable::this_thread* hce::awaitable::this_thread::get() {
    thread_local hce::awaitable::this_thread tlatt;
    return &tlatt;
}

hce::scheduler*& hce::detail::tl_this_scheduler() {
    thread_local hce::scheduler* tlts = nullptr;
    return tlts;
}

hce::scheduler*& hce::detail::tl_this_scheduler_redirect() {
    thread_local hce::scheduler* tlts = nullptr;
    return tlts;
}

#ifndef HCEMINWAITPROC
#define HCEMINWAITPROC 1
#endif

hce::scheduler& hce::scheduler::global() {
    struct schedulizer {
        schedulizer() : 
            sch(hce::scheduler::make(HCEMINWAITPROC)),
            sch_r(*sch),
            thd([&]{ while(sch->run()) { } })
        { }

        ~schedulizer() {
            sch->halt();
            thd.join();
        }

        std::shared_ptr<hce::scheduler> sch;
        hce::scheduler& sch_r;
        std::thread thd;
    };

    static schedulizer schzr;
    return schzer.sch_r;
}

bool& hce::scheduler::worker::tl_is_wait() {
    static bool tl_iw = false;
    return tl_iw;
}
