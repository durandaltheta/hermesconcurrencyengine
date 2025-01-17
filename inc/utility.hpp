//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#ifndef HERMES_COROUTINE_ENGINE_UTILITY
#define HERMES_COROUTINE_ENGINE_UTILITY

#include <utility>
#include <functional>
#include <type_traits>

namespace hce {

template <typename T>
using unqualified = typename std::decay<T>::type;

/// the return type of an arbitrary Callable
template <typename F, typename... Args>
using function_return_type = std::invoke_result_t<F, Args...>;

/// Callable accepting and returning no arguments
typedef std::function<void()> thunk;

}

#endif
