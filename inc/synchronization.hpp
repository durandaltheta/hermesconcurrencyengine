#pragma once
//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#include <mutex>

#include "atomic.hpp"
#include "coroutine.hpp"
#include "scheduler.hpp"
#include "blocking.hpp"

#include <vector>

namespace hce {
namespace mutex {

template <typename LOCKABLE>
hce::awt<void> lock(LOCKABLE& l);

}

namespace detail {
namespace sync {

struct done : 
    public hce::scheduler::reschedule<
        hce::awaitable::lockable<
            hce::lockfree,
            hce::awt_interface<void>>>
{
    done();

    bool on_ready(); 
    void on_resume(void* m);

private:
    hce::lockfree lf_;
};

template <typename UNIQUE_LOCKABLE, typename LOCKABLE>
hce::co<void> construct_unique_lock(UNIQUE_LOCKABLE* ul, LOCKABLE* l) {
    co_await hce::mutex::lock(*l);
    *ul = UNIQUE_LOCKABLE(*l, std::adopt_lock_t{});
    co_return;
}

} // sync
} // detail

namespace mutex {

/**
 @brief a simple way to acquire any atomic lockable object 

 Works with std::mutex
 */
template <typename LOCKABLE>
hce::awt<void> lock(LOCKABLE& l) {
    return l.try_lock()
        ? hce::awt<void>(new hce::detail::sync::done)
        : hce::block([&]{ l.lock(); });
}

} // mutex

namespace unique_lock {

/**
 @brief a simple way to acquire a std::unique_lock object 

 Works with std::unique_lock<std::mutex>
 */
template <typename UNIQUE_LOCKABLE, typename LOCKABLE>
hce::awt<void> construct(UNIQUE_LOCKABLE& ul, LOCKABLE& l) {
    return hce::schedule(hce::detail::sync::construct_unique_lock(&ul, &l));
}

template <typename UNIQUE_LOCKABLE>
hce::awt<void> lock(UNIQUE_LOCKABLE& ul) {
    return ul.try_lock()
        ? hce::awt<void>(new hce::detail::sync::done)
        : hce::block([&]{ ul.lock(); });
}

/**
 @brief a simple way to acquire try_lock_for on a std::unique_lock object 

 Works with std::unique_lock<std::timed_mutex>
 */
template <typename UNIQUE_LOCKABLE, typename DURATION>
hce::awt<bool> try_lock_for(UNIQUE_LOCKABLE& ul, const DURATION& dur) {
    return hce::block([&,dur]() -> bool{ return ul.try_lock_for(dur); });
}

/**
 @brief a simple way to acquire try_lock_until on a std::unique_lock object 

 Works with std::unique_lock<std::timed_mutex>
 */
template <typename UNIQUE_LOCKABLE, typename TIMEPOINT>
hce::awt<bool> try_lock_until(UNIQUE_LOCKABLE& ul, const TIMEPOINT& tp) {
    return hce::block([&,tp]() -> bool { return ul.try_lock_until(tp); });
}

} // unique_lock

namespace condition_variable {

/**
 @brief a simple wait to wait for a duration on a condition 

 Works with std::condition_variable
 */
template <typename CONDITION_VARIABLE, typename LOCK>
hce::awt<void> wait(CONDITION_VARIABLE& cv, LOCK& lk) {
    return hce::block([&]{ cv.wait(lk); });
}

/**
 @brief a simple wait to wait for a duration on a condition 

 Works with std::condition_variable
 */
template <typename CONDITION_VARIABLE, typename LOCK, typename PREDICATE>
hce::awt<void> wait(CONDITION_VARIABLE& cv, LOCK& lk, PREDICATE pred) {
    return hce::block([&,pred]() mutable { cv.wait(lk, pred); });
}

/**
 @brief a simple wait to wait for a duration on a condition

 Works with std::condition_variable
 */
template <typename CONDITION_VARIABLE, typename LOCK, typename DURATION>
hce::awt<void> wait_for(CONDITION_VARIABLE& cv, LOCK& lk, const DURATION& dur) {
    return hce::block([&,dur]{ cv.wait_for(lk, dur); });
}

/**
 @brief a simple wait to wait for a duration on a condition

 Works with std::condition_variable
 */
template <typename CONDITION_VARIABLE, typename LOCK, typename DURATION, typename PREDICATE>
hce::awt<void> wait_for(CONDITION_VARIABLE& cv, LOCK& lk, const DURATION& dur, PREDICATE pred) {
    return hce::block([&,dur,pred]{ cv.wait_for(lk, dur, pred); });
}

/**
 @brief a simple wait to wait until a timeout on a condition

 Works with std::condition_variable
 */
template <typename CONDITION_VARIABLE, typename LOCK, typename TIMEPOINT>
hce::awt<void> wait_until(CONDITION_VARIABLE& cv, LOCK& lk, const TIMEPOINT& tp) {
    return hce::block([&,tp]{ cv.wait_until(lk, tp); });
}

/**
 @brief a simple wait to wait until a timeout on a condition

 Works with std::condition_variable
 */
template <typename CONDITION_VARIABLE, typename LOCK, typename TIMEPOINT, typename PREDICATE>
hce::awt<void> wait_until(CONDITION_VARIABLE& cv, LOCK& lk, const TIMEPOINT& tp, PREDICATE pred) {
    return hce::block([&,tp,pred]{ cv.wait_until(lk, tp, pred); });
}

} // condition_variable
} // hce
