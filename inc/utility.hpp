//SPDX-License-Identifier: Apache-2.0
//Author: Blayne Dennis 
#ifndef __HERMES_COROUTINE_ENGINE_UTILITY__
#define __HERMES_COROUTINE_ENGINE_UTILITY__

#include <utility>
#include <functional>
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

/// Callable accepting and returning no arguments
typedef std::function<void()> thunk;

/**
 @brief call handlers on object destructor

 It is worth pointing out that `T` can be a reference, like `int&`, or a pointer 
 `int*`.
 */
template <typename T>
struct cleanup {
    typedef std::function<void(T)> handler;

    /// construct T
    template <typename... As>
    cleanup(As&&... as) : t_(std::forward<As>(as)...) {}

    /// destructor calls all handlers with T
    ~cleanup() { for(auto& h : handlers_) { h(t_); } }

    /// install handler taking no arguments
    inline void install(thunk th) { 
        handlers_.push_back(adaptor{ std::move(th) });
    }

    /// install handler taking T as an argument
    inline void install(handler h) { 
        handlers_.push_back(std::move(h)); 
    }

private:
    struct adaptor {
        inline void operator()(T) { t(); }
        thunk t;
    };

    T t_;
    std::deque<handler> handlers_; 
};

}

#endif
