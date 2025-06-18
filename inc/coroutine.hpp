//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#ifndef HERMES_COROUTINE_ENGINE_COROUTINE
#define HERMES_COROUTINE_ENGINE_COROUTINE

// c++
#include <memory>
#include <coroutine>
#include <string>
#include <sstream>

// local 
#include "utility.hpp"
#include "cleanup.hpp"
#include "logging.hpp"
#include "memory.hpp"
#include "thread.hpp"
#include "alloc.hpp"
#include "chrono.hpp"
#include "service.hpp"

namespace hce {

/** 
 @brief interface coroutine type 

 An actual coroutine must be an implementation of descendent type co<T> in order 
 to have a valid promise_type.

 Coroutine objects in this library implement unique_ptr-like semantics in order 
 to properly destroy handles. 

 Will print construction/destruction at verbosity 3, and method calls at 
 verbosity 4.
 */
struct coroutine : public printable {
    /**
     This pure virtual interface needs templated methods such as return_void() 
     and return_value(), as well as name() which are implemented by the 
     descendant co<T>::promise_type.
     */
    struct promise_type : public cleanup, public printable {
        promise_type() { }
        virtual ~promise_type(){ }

        // ensure the use of non-throwing operator-new
        static coroutine get_return_object_on_allocation_failure() {
            throw std::bad_alloc();
            return coroutine(nullptr);
        }

        /**
         @brief implement custom new to make use of unified allocation
         */
        inline void* operator new(std::size_t n) noexcept {
            return hce::memory::allocate(n);
        }

        /**
         @brief implement custom delete to make use of unified deallocation
         */
        inline void operator delete(void* ptr) noexcept {
            hce::memory::deallocate(ptr);
        }

        inline coroutine get_return_object() {
            return { 
                std::coroutine_handle<promise_type>::from_promise(*this) 
            };
        }

        inline std::suspend_always initial_suspend() { return {}; }
        inline std::suspend_always final_suspend() noexcept { return {}; }

        void unhandled_exception();
    };

    coroutine();
    coroutine(const coroutine&) = delete;
    coroutine(coroutine&& rhs);

    // construct the coroutine from a type erased handle
    coroutine(std::coroutine_handle<>&& h);

    coroutine& operator=(const coroutine&) = delete;
    coroutine& operator=(coroutine&& rhs);
    virtual ~coroutine();
    static std::string info_name();
    std::string name() const;

    /// return our stringified coroutine handle's address
    std::string content() const;

    /// return true if the handle is valid, else false
    operator bool() const;

    /// releases ownership of the managed handle and returns it
    std::coroutine_handle<> release();

    /// cleans up and resets the managed handle
    void reset();

    /// cleans up and replaces the managed handle
    void reset(std::coroutine_handle<> h);

    /// swap two coroutines
    void swap(coroutine& rhs) noexcept;

    /// return true if the coroutine is done, else false
    bool done() const;

    /// return the address of the underlying handle
    void* address() const;
   
    /// return true if called inside a running coroutine, else false
    static bool in();

    /// return the coroutine running on this thread
    static coroutine& local();

    /// resume the coroutine 
    void resume();

protected:
    void destroy_();

    // the coroutine's managed handle
    std::coroutine_handle<> handle_;
};

/// return the coroutine handle's promise 
template <typename COROUTINE>
inline typename COROUTINE::promise_type& get_promise(COROUTINE& c) {
    return std::coroutine_handle<typename COROUTINE::promise_type>::from_address(
        c.address()).promise();
};

/** 
 @brief stackless management coroutine object with templated return type

 User stackless coroutine implementations must return this object to specify the 
 coroutine return type and select the proper promise_type.

 `hce::coroutine`s and its descendent types act like `std::unique_ptr`s for 
 `std::coroutine_handle<>`s. 
 */
template <typename T>
struct co : public coroutine {
    typedef T value_type;

    struct promise_type : public coroutine::promise_type {
        promise_type() { }
        virtual ~promise_type() { this->clean(); }

        static inline std::string info_name() { 
            return hce::co<T>::info_name() + "::promise_type";
        }

        virtual inline std::string name() const { 
            return hce::co<T>::promise_type::info_name(); 
        }

        /// store the result of `co_return` 
        template <typename TSHADOW>
        inline void return_value(TSHADOW&& t) {
            result = hce::make_unique<T>(std::forward<TSHADOW>(t));
        }

        /**
         @brief the `co_return`ed value of the co<T>

         Is an `hce::unique_ptr` because it deallocates using reusable 
         `hce::memory::cache` mechanism.
         */
        hce::unique_ptr<T> result; 
    };

    typedef std::coroutine_handle<promise_type> handle_type;
    
    co() = default;
    co(const co<T>&) = delete;
    co(co<T>&& rhs) = default;

    virtual ~co(){}

    inline co<T>& operator=(const co<T>&) = delete;
    inline co<T>& operator=(co<T>&& rhs) = default;

    static inline std::string info_name() { 
        return type::templatize<T>("hce::co");
    }

    inline std::string name() const { return co<T>::info_name(); }

    // construct the coroutine from a type erased handle
    co(std::coroutine_handle<> h) : coroutine(std::move(h)) { }

    // base coroutine conversions
    co(const coroutine&) = delete;
    co(coroutine&& rhs) : coroutine(std::move(rhs)) { }

    inline co<T>& operator=(const coroutine&) = delete;

    inline co<T>& operator=(coroutine&& rhs) {
        *this = co<T>(std::move(rhs));
        return *this;
    }
};

template <>
struct co<void> : public coroutine {
    typedef void value_type;

    struct promise_type : public coroutine::promise_type {
        promise_type() { }
        virtual ~promise_type() { this->clean(); }

        static inline std::string info_name() { 
            return co<void>::info_name() + "::promise_type";
        }

        virtual inline std::string name() const { 
            return co<void>::promise_type::info_name(); 
        }

        inline void return_void(){ }
    };

    typedef std::coroutine_handle<promise_type> handle_type;
    
    co() = default;
    co(const co<void>&) = delete;
    co(co<void>&& rhs) = default;

    virtual ~co(){}

    inline co<void>& operator=(const co<void>&) = delete;
    inline co<void>& operator=(co<void>&& rhs) = default;

    static inline std::string info_name() { return "hce::co<void>"; }

    inline std::string name() const { 
        return co<void>::info_name();
    }

    // construct the coroutine from a type erased handle
    co(std::coroutine_handle<> h) : coroutine(std::move(h)) { }

    // base coroutine conversions
    co(const coroutine&) = delete;
    co(coroutine&& rhs) : coroutine(std::move(rhs)) { }

    inline co<void>& operator=(const coroutine&) = delete;

    inline co<void>& operator=(coroutine&& rhs) {
        *this = co<void>(std::move(rhs));
        return *this;
    }
};

/* 
 For some reason templates sometimes need derived templates to satisfy the 
 compiler. `typename` specifiers are confusing to everyone!
 */
template <typename T>
using co_promise_type = typename co<T>::promise_type;

}

#endif
