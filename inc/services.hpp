//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#ifndef HERMES_COROUTINE_ENGINE_SERVICES
#define HERMES_COROUTINE_ENGINE_SERVICES 

#include "scheduler.hpp"
#include "threadpool.hpp"
#include "blocking.hpp"
#include "timer.hpp"
#include "lifecycle.hpp"

namespace hce {
namespace detail {

/**
 Global pointers to service instances are extern, usable by any linked code.
 
 These pointers are used by hce::service<T>::ptr_ref() implementations.
 */

extern hce::scheduler::lifecycle::manager* scheduler_lifecycle_manager_implementation_;
extern hce::scheduler::global* scheduler_global_implementation_;
extern hce::threadpool* threadpool_implementation_;
extern hce::blocking::manager* blocking_implementation_;
extern hce::timer* timer_implementation_;
extern hce::lifecycle* lifecycle_implementation_;

}
}

#endif
