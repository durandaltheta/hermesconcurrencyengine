//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#ifndef __HERMES_COROUTINE_ENGINE_CHRONO__
#define __HERMES_COROUTINE_ENGINE_CHRONO__

#include <chrono>
#include <sstream>

#include "logging.hpp"

namespace hce {
namespace chrono {
namespace detail {

/*
 Due to (potentially developer local) compiler limitations, an alternative 
 duration to stream implementation is needed;
 */
template <typename Rep, typename Period>
inline std::string to_string(const std::chrono::duration<Rep, Period>& d) {
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
inline std::string to_string(const std::chrono::time_point<Rep, Period>& t) {
    return to_string(t.time_since_epoch());
}

}

typedef std::chrono::hours hours;
typedef std::chrono::minutes minutes;
typedef std::chrono::seconds seconds;
typedef std::chrono::milliseconds milliseconds;
typedef std::chrono::microseconds microseconds;
typedef std::chrono::nanoseconds nanoseconds;

/// project wide duration type
struct duration : 
        public std::chrono::steady_clock::duration,
        public printable
{
    duration() : std::chrono::steady_clock::duration() { 
        HCE_TRACE_CONSTRUCTOR();
    }

    template <typename A>
    inline duration(A&& a) : std::chrono::steady_clock::duration(a) { 
        HCE_TRACE_CONSTRUCTOR(hce::chrono::detail::to_string(a));
    }

    ~duration() { HCE_TRACE_DESTRUCTOR(); }

    static inline std::string info_name() { return "hce::chrono::duration"; }
    inline std::string name() const { return duration::info_name(); }
    inline std::string content() const { return hce::chrono::detail::to_string(*this); }

    /**
     @brief convert to a duration to the approximate count of a specified DURATION unit type

     Example valid DURATION values:
     - hce::chrono::hours 
     - hce::chrono::minutes 
     - hce::chrono::seconds 
     - hce::chrono::milliseconds 
     - hce::chrono::microseconds 
     - hce::chrono::nanoseconds  

     @return the count of ticks for the given type
     */
    template <typename DURATION>
    inline size_t to_count() {
        return (size_t)(std::chrono::duration_cast<DURATION>(*this).count());
    }
};

/// project wide time_point type
struct time_point : 
        public std::chrono::steady_clock::time_point,
        public printable
{
    time_point() { HCE_TRACE_CONSTRUCTOR(); }

    template <typename A>
    time_point(A&& a) : std::chrono::steady_clock::time_point(a) { 
        HCE_TRACE_CONSTRUCTOR(hce::chrono::detail::to_string(a));
    }

    ~time_point() { HCE_TRACE_DESTRUCTOR(); }

    static inline std::string info_name() { return "hce::chrono::time_point"; }
    inline std::string name() const { return time_point::info_name(); }
    inline std::string content() const { return hce::chrono::detail::to_string(*this); }

    /// trivial conversion to a duration
    inline operator duration() { return time_since_epoch(); }

    /**
     @brief convert to a duration to the approximate count of a specified DURATION unit type

     Example valid DURATION values:
     - hce::chrono::hours 
     - hce::chrono::minutes 
     - hce::chrono::seconds 
     - hce::chrono::milliseconds 
     - hce::chrono::microseconds 
     - hce::chrono::nanoseconds  

     @return the count of ticks for the given type
     */
    template <typename DURATION>
    inline size_t to_count() {
        return ((hce::chrono::duration)*this).to_count<DURATION>();
    }
};

/// acquire the current time using the library designated clock 
static inline hce::chrono::time_point now() {
    HCE_TRACE_FUNCTION_ENTER("hce::now");
    return { std::chrono::steady_clock::now() };
}

}
}
#endif
