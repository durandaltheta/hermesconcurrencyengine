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
std::string to_string(const std::chrono::duration<Rep, Period>& d) {
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

template <typename Rep, typename Period>
std::string to_string(const std::chrono::time_point<Rep, Period>& t) {
    return to_string(t.time_since_epoch());
}

}

// project wide time units 
enum unit {
    hours,
    minutes,
    seconds,
    milliseconds,
    microseconds,
    nanoseconds
};

/// project wide duration type
struct duration : 
        public std::chrono::steady_clock::duration,
        public hce::printable
{
    duration() : std::chrono::steady_clock::duration() { 
        HCE_TRACE_CONSTRUCTOR();
    }

    template <typename A>
    duration(A&& a) : std::chrono::steady_clock::duration(a) { 
        HCE_TRACE_CONSTRUCTOR(detail::to_string(a));
    }

    ~duration() { HCE_TRACE_DESTRUCTOR(); }

    inline std::string content() const {
        return detail::to_string(*this);
    }
    
    inline const char* nspace() const { return "hce"; }
    inline const char* name() const { return "duration"; }
};

/// project wide time_point type
struct time_point : 
        public std::chrono::steady_clock::time_point,
        public hce::printable
{
    time_point() { HCE_TRACE_CONSTRUCTOR(); }

    template <typename A>
    time_point(A&& a) : std::chrono::steady_clock::time_point(a) { 
        HCE_TRACE_CONSTRUCTOR(detail::to_string(a));
    }

    ~time_point() { HCE_TRACE_DESTRUCTOR(); }

    inline std::string content() const {
        return detail::to_string(time_since_epoch());
    }
    
    inline const char* nspace() const { return "hce"; }
    inline const char* name() const { return "time_point"; }
};

/// acquire the current time
static inline hce::chrono::time_point now() {
    HCE_TRACE_FUNCTION_ENTER("hce::now");
    return { std::chrono::steady_clock::now() };
}

/// convert the time_point to a duration 
static inline hce::chrono::duration to_duration(const hce::chrono::time_point& tp) {
    HCE_TRACE_FUNCTION_ENTER("hce::to_duration", tp);
    return tp.time_since_epoch();
}

/// return the duration 
static inline hce::chrono::duration to_duration(const hce::chrono::duration& dur) {
    HCE_TRACE_FUNCTION_ENTER("hce::to_duration", dur);
    return dur;
}

/// return a duration equivalent to the count of units
static inline hce::chrono::duration to_duration(unit u, size_t count) {
    HCE_TRACE_FUNCTION_ENTER("hce::to_duration", u, count);

    switch(u) {
        case unit::hours:
            return std::chrono::hours(count);
            break;
        case unit::minutes:
            return std::chrono::minutes(count);
            break;
        case unit::seconds:
            return std::chrono::seconds(count);
            break;
        case unit::milliseconds:
            return std::chrono::milliseconds(count);
            break;
        case unit::microseconds:
            return std::chrono::microseconds(count);
            break;
        case unit::nanoseconds:
            return std::chrono::nanoseconds(count);
            break;
        default:
            return std::chrono::nanoseconds(0);
            break;
    }
}

/// return the time_point
static inline hce::chrono::time_point to_time_point(const hce::chrono::time_point& tp) {
    HCE_TRACE_FUNCTION_ENTER("hce::to_time_point", tp);
    return tp;
}

/// add the duration to the current time to get the future time_point
static inline hce::chrono::time_point to_time_point(const hce::chrono::duration& dur) {
    HCE_TRACE_FUNCTION_ENTER("hce::to_time_point", dur);
    return hce::chrono::now() + dur;
}

/// return a time_point equivalent to the count of units in the future
static inline hce::chrono::time_point to_time_point(unit u, size_t count) {
    HCE_TRACE_FUNCTION_ENTER("hce::to_time_point", u, count);
    return hce::chrono::to_time_point(hce::chrono::to_duration(u,count));
}


}
}
#endif
