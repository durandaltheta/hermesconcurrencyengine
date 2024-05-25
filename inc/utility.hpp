//SPDX-License-Identifier: Apache-2.0
//Author: Blayne Dennis 
#ifndef __HERMES_COROUTINE_ENGINE_UTILITY__
#define __HERMES_COROUTINE_ENGINE_UTILITY__

#include <utility>
#include <type_traits>

namespace hce {
namespace detail {

template <typename T>
using unqualified = typename std::decay<T>::type;

template <typename F, typename... Ts>
using function_return_type = typename std::invoke_result<unqualified<F>,Ts...>::type;

template<typename T, typename _ = void>
struct is_container : std::false_type {};

template<typename... Ts>
struct is_container_helper {};

template< bool B, class T, class F >
using conditional_t = typename std::conditional<B,T,F>::type;

template<typename T>
struct is_container<
        T,
        conditional_t<
            false,
            is_container_helper<
                typename T::value_type,
                typename T::iterator,
                decltype(std::declval<T>().begin()),
                decltype(std::declval<T>().end())
                >,
            void
            >
        > : public std::true_type {};

}
}

#endif
