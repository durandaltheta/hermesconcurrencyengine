#pragma once
//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 

#include <cstddef>

#include "hce/chrono.hpp"

namespace hce {
namespace config {
namespace timer {

/**
 Get the platform specific thread priority to pass to hce::set_thread_priority() 
 for the timer processing thread. This priority is expected to be above normal 
 to increase timeout precision.
 */
int thread_priority();

/**
 Busy-waiting is not ideal, but is sometimes necessary to guarantee precision 
 during short timeouts. This value should be set low (IE, less than 10ms) in 
 order to encourage busy-waiting to occur only when timers are very close to 
 timeout. The larger this threshold, the more CPU will be wasted busy-waiting 
 (with potentially increased timeout precision).

 @return threshold in microseconds before timer service will busy wait for timeout
 */
hce::chrono::duration busy_wait_threshold();

/**
 The duration, in microseconds, that the timer service thread should 
 automatically wakeup *early* in order to increase precision of timeouts.

 How this value is used is determined by the timeout_algorithm().

 That is, longer sleeps can have imprecise wakeups due to OS and CPU power 
 saving behavior, so we set an "early" wakeup a short time before timeout so 
 that when the thread goes back to sleep, it's encouraged to wakeup with 
 increased precision the second time.

 @return microsecond early wakeup duration
 */
hce::chrono::duration early_wakeup_threshold();

/**
 An additional duration, in microseconds, that the timer service thread should 
 wakeup early with very long timeouts. 

 How this value is used is determined by the timeout_algorithm().

 This may become necessary because the OS or CPU can enter deeper power saving 
 modes during longer timeouts which can significantly impact timeout precision. 
 This allows the CPU to wakeup somewhat early, hopefully accounting for extra 
 delay and then wait again a much shorter timer before being woken up early at 
 the normal early wakeup threshold.

 The cost for this additional wakeup is quite low, as the longer the timeout
 the less this thread is consuming CPU anyway.

 @return microsecond early wakeup long duration
 */
hce::chrono::duration early_wakeup_long_threshold();

} // timer
} // config
} // hce
