#pragma once
//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
/**
 @file chrono.hpp 

 Standardize chrono time types for this framework
 */
#include <chrono>

#include "hce/logging.hpp"

namespace hce {
namespace chrono {

typedef std::chrono::steady_clock::duration duration;
typedef std::chrono::steady_clock::time_point time_point;

/// acquire the current time using the library designated clock 
static inline std::chrono::steady_clock::time_point now() {
    return std::chrono::steady_clock::now();
}

/// convenience duration cast
template <typename Duration, typename Rep, typename Period>
inline Duration to(const std::chrono::duration<Rep,Period>& dur) {
    return std::chrono::duration_cast<Duration>(dur);
}

} // chrono

namespace type {

template <>
struct info<std::chrono::steady_clock,void> {
    static inline std::string name(){ return "std::chrono::steady_clock"; }
};


template <typename Rep, typename Period>
struct info<std::chrono::duration<Rep,Period>,void> {
    static inline std::string name(){ 
        return templatize<Rep,Period>("std::chrono::duration");
    }
};

template <typename Clock, typename Duration>
struct info<std::chrono::time_point<Clock,Duration>,void> {
    static inline std::string name(){ 
        return templatize<Clock,Duration>("std::chrono::time_point"); 
    }
};

} // type

// time-to-string conversions
namespace chrono {
namespace detail {

template <typename Rep, typename Period>
inline void ingest_duration_ticks(
        std::stringstream& ss,
        const std::chrono::duration<Rep, Period>& d)
{
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
}

}

template <typename Rep, typename Period>
inline std::string to_string(const std::chrono::duration<Rep, Period>& d) {
    std::stringstream ss;
    ss << hce::type::name<const std::chrono::duration<Rep, Period>&>() << "[";
    detail::ingest_duration_ticks(ss, d);
    ss << "]";
    return ss.str();
}

template <typename Clock, typename Duration>
inline std::string to_string(const std::chrono::time_point<Clock, Duration>& t) 
{
    std::stringstream ss;
    ss << hce::type::name<const std::chrono::time_point<Clock, Duration>&>() << "[";
    detail::ingest_duration_ticks(ss, t.time_since_epoch());
    ss << "]";
    return ss.str();
}

} // chrono

namespace detail {
namespace logger {

// template specializations for duration
template <class Rep, class Period>
static inline void ingest_item_(
        std::stringstream& ss, 
        const std::chrono::duration<Rep,Period>& d) 
{
    ss << hce::chrono::to_string(d);
}

template <class Rep, class Period>
static inline void ingest_item_(
        std::stringstream& ss, 
        std::chrono::duration<Rep,Period>& d) 
{
    ss << hce::chrono::to_string(d);
}

template <class Rep, class Period>
static inline void ingest_item_(
        std::stringstream& ss, 
        std::chrono::duration<Rep,Period>&& d) 
{
    ss << hce::chrono::to_string(d);
}

// template specializations for time_point
template <class Clock, class Duration>
static inline void ingest_item_(
        std::stringstream& ss, 
        const std::chrono::time_point<Clock, Duration>& tp) 
{
    ss << hce::chrono::to_string(tp);
}

template <class Clock, class Duration>
static inline void ingest_item_(
        std::stringstream& ss, 
        std::chrono::time_point<Clock, Duration>& tp) 
{
    ss << hce::chrono::to_string(tp);
}

template <class Clock, class Duration>
static inline void ingest_item_(
        std::stringstream& ss, 
        std::chrono::time_point<Clock, Duration>&& tp) 
{
    ss << hce::chrono::to_string(tp);
}

} // logger 
} // detail

} // hce
