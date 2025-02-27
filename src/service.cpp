#include "utility.hpp"
#include "thread.hpp"
#include "scheduler.hpp"
#include "threadpool.hpp"
#include "blocking.hpp"
#include "timer.hpp"
#include "lifecycle.hpp"
#include "service.hpp"

HCE_EXPORT hce::thread::local* local_implementation_ = nullptr;
HCE_EXPORT hce::scheduler::lifecycle::manager* scheduler_lifecycle_manager_implementation_ = nullptr;
HCE_EXPORT hce::scheduler::global* scheduler_global_implementation_ = nullptr;
HCE_EXPORT hce::threadpool* threadpool_implementation_ = nullptr;
HCE_EXPORT hce::blocking* blocking_implementation_ = nullptr;
HCE_EXPORT hce::timer* timer_implementation_ = nullptr;
HCE_EXPORT hce::lifecycle* lifecycle_implementation_ = nullptr;

template <>
hce::thread::local*& hce::service<hce::thread::local>::ptr_ref() {
    return local_implementation_;
}

template <>
hce::scheduler::lifecycle::manager*& hce::service<hce::scheduler::lifecycle::manager>::ptr_ref() {
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
hce::blocking*& hce::service<hce::blocking>::ptr_ref() {
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
