//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#ifndef HERMES_COROUTINE_ENGINE_THREAD_KEY_MAP
#define HERMES_COROUTINE_ENGINE_THREAD_KEY_MAP

#include "thread.hpp"
#include "list.hpp"
#include "coroutine.hpp"
#include "scheduler.hpp"

namespace hce {
namespace thread {

template <>
struct key_map_t<key::loglevel> { using type = int; };

template <>
struct key_map_t<key::coroutine> { using type = hce::coroutine; };

template <>
struct key_map_t<key::coroutine_thread> { 
    using type = hce::detail::coroutine::this_thread; 
};

template <>
struct key_map_t<key::scheduler> { using type = hce::scheduler; };

template <>
struct key_map_t<key::scheduler_local_queue> { 
    using type = std::unique_ptr<hce::list<std::coroutine_handle<>>>; 
};

}
}

#endif
