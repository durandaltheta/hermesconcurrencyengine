//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#include <string>
#include <sstream>
#include <thread>

#include "loguru.hpp"
#include "atomic.hpp"
#include "thread.hpp"
#include "coroutine.hpp"
#include "thread_key_map.hpp"

hce::coroutine*& hce::detail::coroutine::tl_this_coroutine() {
    thread_local hce::thread::local::ptr<hce::thread::key::coroutine> p;
    return p.ref();
}

hce::detail::coroutine::this_thread* 
hce::detail::coroutine::this_thread::get() {
    struct init {
        init() {
            if(ptr.ref() == nullptr) {
                obj.reset(new hce::detail::coroutine::this_thread);
                ptr.ref() = obj.get();
            }
        }

        std::unique_ptr<hce::detail::coroutine::this_thread> obj;
        hce::thread::local::ptr<hce::thread::key::coroutine_thread> ptr;
    };

    thread_local init i;
    return i.ptr.ref();
}
