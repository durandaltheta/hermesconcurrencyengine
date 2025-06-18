#include "scheduler.hpp"
#include "threadpool.hpp"
#include "blocking.hpp"
#include "timer.hpp"
#include "lifecycle.hpp"
#include "services.hpp"

static hce::scheduler::lifecycle::manager* scheduler_lifecycle_manager_implementation_ = nullptr;
static hce::scheduler::global* scheduler_global_implementation_ = nullptr;
static hce::threadpool* threadpool_implementation_ = nullptr;
static hce::blocking::manager* blocking_implementation_ = nullptr;
static hce::timer* timer_implementation_ = nullptr;
static hce::lifecycle* lifecycle_implementation_ = nullptr;

template <>
hce::scheduler::lifecycle::manager*& 
hce::service<hce::scheduler::lifecycle::manager>::ptr_ref() {
    return scheduler_lifecycle_manager_implementation_;
}

template <>
hce::scheduler::global*& hce::service<hce::scheduler::global>::ptr_ref() {
    return scheduler_global_implementation_;
}

template <>
hce::threadpool*& hce::service<hce::threadpool>::ptr_ref() {
    return threadpool_implementation_;
}

template <>
hce::blocking::manager*& hce::service<hce::blocking::manager>::ptr_ref() {
    return blocking_implementation_;
}

template <>
hce::timer*& hce::service<hce::timer>::ptr_ref() {
    return timer_implementation_;
}

template <>
hce::lifecycle*& hce::service<hce::lifecycle>::ptr_ref() {
    return lifecycle_implementation_;
}
