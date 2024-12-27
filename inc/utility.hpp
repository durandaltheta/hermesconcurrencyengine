//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#ifndef __HERMES_COROUTINE_ENGINE_UTILITY__
#define __HERMES_COROUTINE_ENGINE_UTILITY__

#include <utility>
#include <functional>
#include <type_traits>

namespace hce {

template <typename T>
using unqualified = typename std::decay<T>::type;

/// the return type of an arbitrary Callable
template <typename F, typename... Args>
using function_return_type = typename std::function<std::result_of_t<F(Args...)>()>;

/// Callable accepting and returning no arguments
typedef std::function<void()> thunk;

}

#endif
