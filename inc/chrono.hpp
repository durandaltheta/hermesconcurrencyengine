//SPDX-License-Identifier: Apache-2.0
//Author: Blayne Dennis 
#ifndef __HCE_COROUTINE_ENGINE_CHRONO__
#define __HCE_COROUTINE_ENGINE_CHRONO__

#include <chrono>
#include <sstream>

#include "utility.hpp"

namespace hce {
namespace chrono {
namespace detail {

/*
 Due to (potentially developer local) compiler limitations, an alternative 
 duration to stream implementation is needed;
 */
template <typename Rep, typename Period>
std::string duration_to_string(const std::chrono::duration<Rep, Period>& d) {
    std::stringstream ss;
    ss << d.count() << " ";

    if (std::ratio_equal<Period, std::nano>::value) {
        ss << "ns";
    } else if (std::ratio_equal<Period, std::micro>::value) {
        ss << "µs";
    } else if (std::ratio_equal<Period, std::milli>::value) {
        ss << "ms";
    } else if (std::ratio_equal<Period, std::ratio<1>>::value) {
        ss << "s";
    } else if (std::ratio_equal<Period, std::ratio<60>>::value) {
        ss << "min";
    } else if (std::ratio_equal<Period, std::ratio<3600>>::value) {
        ss << "h";
    } else {
        ss << "unknown_period";
    }

    return ss.str();
}

}

// project wide time units 
enum unit {
    hour,
    minute,
    second,
    millisecond,
    microsecond,
    nanosecond
};

/// project wide time_point type
struct time_point : 
        public std::chrono::steady_clock::time_point,
        public hce::printable
{
    template <typename... As>
    time_point(As&&... as) : 
        std::chrono::steady_clock::time_point(as...)
    { 
        HCE_TRACE_CONSTRUCTOR(as...);
    }

    ~time_point() { HCE_TRACE_DESTRUCTOR(); }

    inline std::string content() const {
        return detail::duration_to_string(time_since_epoch());
    }
    
    inline const char* nspace() const { return "hce"; }
    inline const char* name() const { return "time_point"; }
};

/// project wide duration type
struct duration : 
        public std::chrono::steady_clock::duration,
        public hce::printable
{
    template <typename... As>
    duration(As&&... as) : 
        std::chrono::steady_clock::duration(as...)
    { 
        HCE_TRACE_CONSTRUCTOR(as...);
    }

    ~duration() { HCE_TRACE_DESTRUCTOR(); }

    inline std::string content() const {
        return detail::duration_to_string(*this);
    }
    
    inline const char* nspace() const { return "hce"; }
    inline const char* name() const { return "duration"; }
};

/// acquire the current time
static inline hce::chrono::time_point now() {
    HCE_TRACE_METHOD_ENTER("now");
    return std::chrono::steady_clock::now();
}

/// convert the time_point to a duration 
static inline hce::chrono::duration to_duration(const hce::chrono::time_point& tp) {
    HCE_TRACE_METHOD_ENTER("to_duration", tp);
    return tp.time_since_epoch();
}

/// return the duration 
static inline hce::chrono::duration to_duration(const hce::chrono::duration& dur) {
    HCE_TRACE_METHOD_ENTER("to_duration", dur);
    return dur;
}

/// return a duration equivalent to the count of units
static inline hce::chrono::duration to_duration(unit u, size_t count) {
    HCE_TRACE_METHOD_ENTER("to_duration", u, count);

    switch(u) {
        case unit::hour:
            return std::chrono::hours(count);
            break;
        case unit::minute:
            return std::chrono::minutes(count);
            break;
        case unit::second:
            return std::chrono::seconds(count);
            break;
        case unit::millisecond:
            return std::chrono::milliseconds(count);
            break;
        case unit::microsecond:
            return std::chrono::microseconds(count);
            break;
        case unit::nanosecond:
            return std::chrono::nanoseconds(count);
            break;
        default:
            return std::chrono::nanoseconds(0);
            break;
    }
}

/// return the time_point
static inline hce::chrono::time_point to_time_point(const hce::chrono::time_point& tp) {
    HCE_TRACE_METHOD_ENTER("to_time_point", tp);
    return tp;
}

/// convert the duration to a time_point
static inline hce::chrono::time_point to_time_point(const hce::chrono::duration& dur) {
    HCE_TRACE_METHOD_ENTER("to_time_point", dur);
    return hce::chrono::now() + dur;
}

/// return a time_point equivalent to the count of units in the future
static inline hce::chrono::time_point to_time_point(unit u, size_t count) {
    HCE_TRACE_METHOD_ENTER("to_time_point", u, count);
    return hce::chrono::to_time_point(hce::chrono::to_duration(u,count));
}


}
}
#endif
