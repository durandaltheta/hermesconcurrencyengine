//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
/**
 @file chrono.hpp 

 Standardize chrono time types for this framework
 */
#ifndef HERMES_COROUTINE_ENGINE_CHRONO
#define HERMES_COROUTINE_ENGINE_CHRONO

#include <chrono>

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

}
}
#endif
