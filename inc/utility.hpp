//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#ifndef __HERMES_COROUTINE_ENGINE_UTILITY__
#define __HERMES_COROUTINE_ENGINE_UTILITY__

#include <utility>
#include <functional>
#include <type_traits>

namespace hce {

struct no_init { };
struct pre_cache { size_t size=0; };

template <typename T>
using unqualified = typename std::decay<T>::type;

/// the return type of an arbitrary Callable
template <typename F, typename... Ts>
using function_return_type = typename std::invoke_result<F,Ts...>::type;

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

/// Callable accepting and returning no arguments
typedef std::function<void()> thunk;

/// structure used for sending from a source to a destination pointer
template <typename T>
struct send_pair {
    send_pair() : source_(nullptr), is_rvalue_(false) { }

    send_pair(void* source, bool is_rvalue) : 
        source_(source), 
        is_rvalue_(is_rvalue) 
    { }

    inline void send(void* destination) {
        if(is_rvalue_) [[likely]] { // optimize for low cost move
            *((T*)destination) = std::move(*((T*)(source_))); 
        } else {
            *((T*)destination) = *((const T*)(source_)); 
        }
    }

private:
    void* source_;
    bool is_rvalue_;
};

}

#endif
