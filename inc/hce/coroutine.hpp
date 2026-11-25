#pragma once
//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 

#include <exception>
#include <condition_variable>
#include <coroutine>

#include "hce/printable.hpp"
#include "hce/utility/cleanup.hpp"
#include "hce/utility/alloc.hpp"
#include "hce/utility/fatal.hpp"
#include "hce/utility/type.hpp"

namespace hce {

struct coroutine;

namespace detail {
namespace coroutine {

// always points to the coroutine running on this thread
hce::coroutine*& tl_this_coroutine();

} // detail
} // coroutine

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
     co<T>::promise_type.
     */
    struct promise_type : public cleanup, public printable {
        promise_type();
        virtual ~promise_type();

        // ensure the use of non-throwing operator-new
        static coroutine get_return_object_on_allocation_failure();

        /// implement cleanup interface
        void* cleanup_alloc(size_t n);

        /// implement cleanup interface
        void cleanup_dealloc(void* p);

        /**
         @brief implement custom new to make use of thread local allocation caching
         */
        void* operator new(std::size_t n) noexcept;

        /**
         @brief implement custom delete to make use of thread local allocation caching 
         */
        void operator delete(void* ptr) noexcept;

        coroutine get_return_object();

        std::suspend_always initial_suspend();
        std::suspend_always final_suspend() noexcept;
        void unhandled_exception();

        /// exception pointer to the most recently raised exception
        std::exception_ptr eptr = nullptr; 
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
        virtual ~promise_type() { clean(); }

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
        virtual ~promise_type() { clean(); }

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

namespace detail {
namespace coroutine {

/// thread_local block/unblock functionality
struct this_thread : public printable {
    static std::string info_name();
    std::string name() const;

    // get the this_thread object associated with the calling thread
    static this_thread* get();

    // can only block the calling thread
    template <typename LOCK>
    static inline void block(LOCK& lk) {
        auto tt = this_thread::get();
        tt->block_(lk);
    }
    
    // unblock an arbitrary this_thread without lock synchronization
    void unblock();

    // unblock an arbitrary this_thread with lock synchronization
    template <typename LOCK>
    inline void unblock(LOCK& lk) {
        ready = true;
        lk.unlock();
        cv.notify_one();
    }

private:
    this_thread();

    template <typename LOCK>
    inline void block_(LOCK& lk) {
        while(!ready) { 
            cv.wait(lk); 
        }

        ready = false; // reset flag
    }

    bool ready = false;
    std::condition_variable_any cv;
};

}

struct yield : public hce::printable {
    yield();
    virtual ~yield();

    static std::string info_name();
    std::string name() const;
    bool await_ready();

    // don't need to replace the handle, it's not moving
    void await_suspend(std::coroutine_handle<> h);

private:
    bool awaited_ = false;
};

}

/**
 @brief `co_await` to suspend execution and allow other coroutines to run. 

 This is an awaitable for usage with the `co_await` keyword.

 This object is used to suspend execution to the caller of `coroutine::resume()` 
 in order to let other coroutines run. `coroutine::done()` will `== false` after 
 suspending in this manner, so the coroutine can be requeued immediately. This 
 may be necessary to prevent CPU starvation due to long running coroutines 
 preventing evaluation of other coroutines.

 Even though the name of this object is `yield`, it does *NOT* use the 
 `co_yield` keyword. `yield<T>` returns a value `T` to the *caller of co_await* 
 (*not* coroutine::resume()!). 

 If used by non-coroutines no suspend will occur, and the value is returned 
 immediately. Unlike `awt<T>`, which requires an implementation, this object 
 can be `co_await`ed directly.

 To temporarily suspend to caller of coroutine `resume()`:
 ```
 int i = co_await hce::yield<int>(3); // i == 3 when coroutine resumes
 ```
 */
template <typename T>
struct yield : public detail::yield {
    typedef T value_type;

    template <typename... As>
    yield(As&&... as) : t(std::forward<As>(as)...) { }

    static inline std::string info_name() { 
        return type::templatize<T>("hce::yield"); 
    }

    inline std::string name() const { return yield<T>::info_name(); }

    inline T await_resume() { 
        return std::move(t); 
    }

    inline operator T() {
        if(coroutine::in()) [[unlikely]] { 
            hce::fatal(
                hce::coroutine::local(),
                "did not call co_await on ",
                *this);
        }

        return await_resume(); 
    }

private:
    T t;
};

/**
 void yield specialization.

 To suspend to caller of `coroutine::resume()`:
 ```
 co_await hce::yield<void>(); 
 ```
 */
template <>
struct yield<void> : public detail::yield {
    typedef void value_type;

    static inline std::string info_name() { 
        return type::templatize<void>("hce::yield"); 
    }

    inline std::string name() const { return yield<void>::info_name(); }
    inline void await_resume() { }
};

}
