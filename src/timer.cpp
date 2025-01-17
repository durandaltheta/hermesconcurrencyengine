//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#include "timer.hpp"
#include "lifecycle.hpp"

hce::timer::service* hce::timer::service::instance_ = nullptr;

hce::timer::service& hce::timer::service::get() {
    return *(hce::timer::service::instance_);
}

hce::timer::service::service() : 
    running_(true),
    waiting_(false),
    micro_runtime_ticks_(0),
    micro_busywait_ticks_(0),
    busy_wait_threshold_(hce::config::timer::service::busy_wait_threshold()),
    timeout_algorithm_(hce::config::timer::service::timeout_algorithm())
{
    thd_ = std::thread([](service* ts) { 
        HCE_HIGH_FUNCTION_ENTER("hce::timer::service::thread");
        ts->run(); 
        HCE_HIGH_FUNCTION_BODY("hce::timer::service::thread","exit");
    }, this);

    hce::set_thread_priority(
        thd_, 
        hce::config::timer::service::thread_priority());

    HCE_HIGH_CONSTRUCTOR();
}

hce::chrono::time_point hce::timer::service::default_timeout_algorithm(
        const hce::chrono::time_point& now, 
        const hce::chrono::time_point& requested_timeout)
{
    static const auto btw = hce::config::timer::service::busy_wait_threshold();
    static const auto ewt = hce::config::timer::service::early_wakeup_threshold();
    static const auto ewlt = hce::config::timer::service::early_wakeup_long_threshold();
    auto timeout = requested_timeout;
    auto requested_timeout_dur = requested_timeout - now;

    if(requested_timeout_dur > ewlt) {
        // try to wakeup long sleeps extra early to account for slower behaviors 
        // like power saving, hopefully increasing precision
        timeout = requested_timeout - ewlt;
    } else {
        if(requested_timeout_dur < ewt) {
            // since we are not currently busy waiting try to wakeup during 
            // the busy-wait window
            timeout = requested_timeout - btw;
        } else {
            // as we approach timeout, continously wakeup in 
            // small increments to keep precision high (CPU less likely to 
            // power save) and increase the chance of waking up within the busy 
            // wait threshold
            timeout = now + ewt;
        }
    }

    return timeout;
}
