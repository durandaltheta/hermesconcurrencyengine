//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#ifndef HERMES_COROUTINE_ENGINE_THREAD
#define HERMES_COROUTINE_ENGINE_THREAD

#include <thread>

namespace hce {
namespace thread {

/**
 @brief attempt to set a thread's priority in a system agnostic way 
 */
bool set_priority(std::thread& thr, int priority);

}

}

#endif
