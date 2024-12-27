//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#ifndef __HERMES_COROUTINE_ENGINE_THREAD__
#define __HERMES_COROUTINE_ENGINE_THREAD__

#include <thread>

// Platform-specific includes
#ifdef _WIN32
#include <windows.h>
#elif defined(_POSIX_VERSION)
#include <pthread.h>
#include <sched.h>
#endif

#include "logging.hpp"

namespace hce {

/**
 @brief attempt to set a thread's priority in a system agnostic way 

 */
inline bool set_thread_priority(std::thread& thr, int priority) {
#if defined(_WIN32) || defined(_WIN64)
    HANDLE handle = static_cast<HANDLE>(thr.native_handle());
    bool success = SetThreadPriority(handle, priority) != 0;
    HCE_ERROR_GUARD(!success, HCE_ERROR_FUNCTION_BODY("set_thread_priority", "Failed to set thread scheduling policy on Window: SetThreadPriority() returned 0"));
    return success;
#elif defined(_POSIX_VERSION)
    pthread_t native = thr.native_handle();
    struct sched_param param;
    int policy;

    if (pthread_getschedparam(native, &policy, &param) != 0) {
        HCE_ERROR_FUNCTION_BODY("set_thread_priority", "Failed to get thread scheduling policy on POSIX: Unable to get thread scheduling parameters");
        return false; 
    }

    if (policy == SCHED_OTHER) {
        // Only adjust priority within SCHED_OTHER's valid range.
        param.sched_priority = priority; // Valid range: implementation-defined.
        bool success = pthread_setschedparam(native, policy, &param) == 0;

        HCE_ERROR_GUARD(!success, HCE_ERROR_FUNCTION_BODY("set_thread_priority", "Failed to get thread scheduling policy on POSIX: Unable to set scheduler parameters"));

        return success;
    }

    HCE_INFO_FUNCTION_BODY("set_thread_priority", "Count not set thread scheduling policy on POSIX: Unsupported policy for non-superuser adjustment");
    return false;
#else
    HCE_ERROR_FUNCTION_BODY("set_thread_priority", "Failed to set thread scheduling policy: Unsupported platform");
    return false;
#endif
}

}

#endif
