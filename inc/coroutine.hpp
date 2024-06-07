//SPDX-License-Identifier: Apache-2.0
//Author: Blayne Dennis 
#ifndef __HERMES_COROUTINE_ENGINE_COROUTINE__
#define __HERMES_COROUTINE_ENGINE_COROUTINE__

// c++
#include <memory>
#include <mutex>
#include <condition_variable>
#include <coroutine>
#include <exception>
#include <any>
#include <deque>
#include <functional>

// local 
#include "atomic.hpp"
#include "utility.hpp"

namespace hce {

/** 
 @brief interface coroutine type 

 An actual coroutine must be an implementation of descendant type co<T> in order 
 to have a valid promise_type.
 */
struct coroutine {
    /**
     The base promise object for stackless coroutines, used primarily by the 
     compiler when it constructs stackless coroutines.

     This object is a pure virtual interface. Additionally, it needs expected 
     templated methods such as return_void()/return_value(), which are 
     implemented by co<T>.
     */
    struct promise_type {
        virtual ~promise_type() { }

        inline coroutine get_return_object() {
            return { 
                std::coroutine_handle<promise_type>::from_promise(*this) 
            };
        }

        inline std::suspend_always initial_suspend() { return {}; }
        inline std::suspend_always final_suspend() noexcept { return {}; }
        inline void unhandled_exception() { eptr = std::current_exception(); }

        /// return actual co<T>::promise_type type
        virtual const std::type_info& type_info() = 0;

        std::exception_ptr eptr = nullptr; // exception pointer
    };

    coroutine() { }

    coroutine(const coroutine&) = delete;
    coroutine(coroutine&& rhs) { swap(rhs); }

    // construct the coroutine from a type erased handle
    coroutine(std::coroutine_handle<> h) : handle_(h) { }

    inline coroutine& operator=(const coroutine&) = delete;

    inline coroutine& operator=(coroutine&& rhs) { 
        swap(rhs);
        return *this;
    }

    virtual ~coroutine() { reset(); }

    /// return true if the handle is valid, else false
    inline operator bool() const { return (bool)handle_; }

    /// returns a the managed handle and releases the ownership
    inline std::coroutine_handle<> release() {
        auto h = handle_;
        handle_ = std::coroutine_handle<>();
        return h;
    }

    /// replaces the managed handle
    inline void reset() { 
        if(handle_) { handle_.destroy(); }
        handle_ = std::coroutine_handle<>(); 
    }

    /// replaces the managed handle
    inline void reset(std::coroutine_handle<> h) { 
        if(handle_) { handle_.destroy(); }
        handle_ = h; 
    }

    inline void swap(coroutine& rhs) noexcept { 
        if(handle_) {
            auto h = handle_;
            handle_ = rhs.handle_;
            rhs.handle_ = h;
        } else {
            handle_ = rhs.handle_;
            rhs.handle_ = std::coroutine_handle<>();
        }
    }

    /// return true if the coroutine is done, else false
    inline bool done() const { return handle_.done(); }

    /**
     @brief cast the coroutine's handle to the descendant type's and 
     return the promise.

     Will need to call this on a coroutine to get the actual promise. When 
     expecting coroutine<T>:
     ```
     hce::coroutine my_co = hce::coroutine<T>();
     hce::coroutine<T>::promise_type& my_promise = my_co.to_promise<hce::coroutine<T>>();
     ```
     */
    template <typename COROUTINE>
    inline typename COROUTINE::promise_type& to_promise() const {
        return std::coroutine_handle<typename COROUTINE::promise_type>::from_address(
                handle_.address()).promise();
    }
   
    /// return true if called inside a running coroutine, else false
    static inline bool in() { 
        return coroutine::tl_this_coroutine(); 
    }

    /// return the coroutine running on this thread
    static inline coroutine& local() { 
        return *(coroutine::tl_this_coroutine()); 
    }

    /// resume the coroutine 
    inline void resume() {
        auto& tl_co = coroutine::tl_this_coroutine();

        // store parent coroutine pointer
        auto parent_co = tl_co;

        // set current coroutine ptr 
        tl_co = this; 
        
        // continue coroutine execution
        try {
            handle_.resume();

            if(handle_) {
                // acquire the exception pointer
                auto eptr = std::coroutine_handle<promise_type>::from_address(
                    handle_.address()).promise().eptr;

                // rethrow any exceptions from the coroutine
                if(eptr) { std::rethrow_exception(eptr); }
            }
        } catch(...) {
            // restore parent coroutine ptr
            tl_co = parent_co;
            std::rethrow_exception(std::current_exception());
        }
    }

protected:
    // always points to the coroutine running on the scheduler on this thread
    static coroutine*& tl_this_coroutine();

    // the coroutine's managed handle
    std::coroutine_handle<> handle_;
};

/** 
 @brief stackless management coroutine object with templated return type

 User stackless coroutine implementations must return this object to specify the 
 coroutine return type and select the proper promise_type.

 `hce::coroutine` act like `std::unique_ptr`s for `std::coroutine_handle<>`s.
 If an `hce::coroutine` owns a valid handle when it goes out of scope it will 
 call the handle's `destroy()` operation.
 */
template <typename T>
struct co : public coroutine {
    struct promise_type : public coroutine::promise_type {
        /// ensure cleanup handlers are run before promise_type destructs
        virtual ~promise_type(){ cleanup_.reset(); }

        inline const std::type_info& type_info() { 
            return typeid(co<T>::promise_type); 
        }

        /**
         @brief store the result of `co_return` 

         Use a new template type to prevent template type shadowing errors and 
         retain universal reference semantics.
         */ 
        template <typename TSHADOW>
        inline void return_value(TSHADOW&& t) {
            result = std::forward<TSHADOW>(t);
        }

        /// install a coroutine<T>::promise_type::cleanup::handler
        template <typename HANDLER>
        inline void install(HANDLER&& hdl) {
            if(!cleanup_) { 
                cleanup_ = 
                    std::unique_ptr<hce::cleanup<promise_type&>>(
                        new hce::cleanup<promise_type&>(*this)); 
            }

            cleanup_->install(std::forward<HANDLER>(hdl));
        }

        /// the result of the coroutine<T>
        T result; 

    private: 
        std::unique_ptr<hce::cleanup<promise_type&>> cleanup_;
    };

    typedef std::coroutine_handle<promise_type> handle_type;

    /// return the promise associated with this coroutine's handle
    inline promise_type& promise() const {
        return handle_type::from_address(handle_.address()).promise();
    }
    
    co() = default;
    co(const co<T>&) = delete;
    co(co<T>&& rhs) = default;

    inline co<T>& operator=(const co<T>&) = delete;
    inline co<T>& operator=(co<T>&& rhs) = default;

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
    struct promise_type : public coroutine::promise_type {
        /// ensure cleanup handlers are run before promise_type destructs
        virtual ~promise_type(){ cleanup_.reset(); }

        inline const std::type_info& type_info() { 
            return typeid(co<void>::promise_type); 
        }

        inline void return_void() {}

        /// install a coroutine<T>::promise_type::cleanup::handler
        template <typename HANDLER>
        inline void install(HANDLER&& hdl) {
            if(!cleanup_) { 
                cleanup_ = 
                    std::unique_ptr<hce::cleanup<promise_type&>>(
                        new hce::cleanup<promise_type&>(*this)); 
            }

            cleanup_->install(std::forward<HANDLER>(hdl));
        }

    private: 
        std::unique_ptr<hce::cleanup<promise_type&>> cleanup_;
    };

    typedef std::coroutine_handle<promise_type> handle_type;

    /// return the promise associated with this coroutine's handle
    inline promise_type& promise() const {
        return handle_type::from_address(handle_.address()).promise();
    }
    
    co() = default;
    co(const co<void>&) = delete;
    co(co<void>&& rhs) = default;

    inline co<void>& operator=(const co<void>&) = delete;
    inline co<void>& operator=(co<void>&& rhs) = default;

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

namespace detail {
namespace coroutine {

/// thread_local block/unblock functionality
struct this_thread {
    // get the this_thread object associated with the calling thread
    static this_thread* get();

    // can only block the calling thread
    template <typename LOCK>
    static inline void block(LOCK& lk) {
        this_thread::get()->block_(lk);
    }

    // unblock an arbitrary this_thread
    template <typename LOCK>
    inline void unblock(LOCK& lk) {
        ready = true;
        lk.unlock();
        cv.notify_one();
    }

private:
    this_thread() : ready{false} { }

    template <typename LOCK>
    inline void block_(LOCK& lk) {
        while(!(ready)) { cv.wait(lk); }
        ready = false;
    }

    bool ready;
    std::condition_variable_any cv;
};

template <typename T>
inline hce::co<T> wrapper(std::function<T()> f) {
    co_return f();
}

template <>
inline hce::co<void> wrapper(std::function<void()> f) {
    f();
    co_return;
}

void coroutine_did_not_co_await(void* awt);
void awaitable_not_resumed(void* awt, void* hdl);

}

struct yield {
    ~yield() {
        if(hce::coroutine::in() && !awaited_){ 
            detail::coroutine::coroutine_did_not_co_await(this); 
        }
    }

    inline bool await_ready() {
        awaited_ = true;
        return false; 
    }

    // don't need to replace the handle, it's not moving
    inline void await_suspend(std::coroutine_handle<> h) { }

private:
    bool awaited_ = false;
};

}

/**
 @brief wrap an arbitrary Callable and optional arguments as a coroutine<T>

 A utility to wrap any Callable as a coroutine for execution. A Callable is any 
 invokable object such as function pointers, Functors or lambdas.

 The resulting coroutine<T> will not have any special features, when resume()d
 it will execute cb(as...) and `co_return` the result (if result is non-void). 
 This allows execution of arbitary code as a coroutine if necessary.

 @param cb a Callable
 @param as arguments to the Callable
 @return a coroutine<T>
 */
template <typename Callable, typename... As>
auto 
to_coroutine(Callable&& cb, As&&... as) {
    return detail::coroutine::wrapper(
        std::bind(
            std::forward<Callable>(cb),
            std::forward<As>(as)...));
}

/**
 @brief `co_await` to suspend execution and allow other coroutines to run. 

 This is an awaitable for usage with the `co_await` keyword.

 This object is used to suspend execution to the caller of `coroutine::resume()` 
 in order to let other coroutines run. `coroutine::done()` will `== false` after 
 suspending in this manner, so the coroutine can be requeued immediately.

 Even though the name of this object is `yield`, it does *NOT* use the 
 `co_yield` keyword. `yield<T>` returns a value `T` to the *caller of co_await* 
 (*not* coroutine::resume()!) which is returned on resumption. 


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
    template <typename... As>
    yield(As&&... as) : t(std::forward<As>(as)...) { }

    inline T await_resume() { return std::move(t); }

    inline operator T() {
        if(coroutine::in()) { 
            detail::coroutine::coroutine_did_not_co_await(this); 
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
    inline void await_resume() { }
};

/**
 @brief complex awaitables inherit shared functionality defined here

 `hce::awaitable` objects cannot be used by themselves, they must be inheritted 
 by another object (IE, `hce::awt<T>`) which implements the `await_resume()` 
 method.

 This object requires implementation of various interfaces. It does this as a 
 type erasure strategy that allows for maintenance of a single unique pointer 
 to an underlying implementation. Because of this it can be a bit awkward to 
 write new implementations, but the utilities in this library should accomodate 
 most user needs without requiring additional implementations.
 */
struct awaitable {
    /**
     @brief pure virtual interface for an awaitable's implementation 

     Implements methods required by an awaitable to function in a coroutine-safe 
     and thread-safe way.
     */
    struct interface {
        virtual ~interface() { 
            if(handle_) {
                detail::coroutine::awaitable_not_resumed(this, handle_.address());

                // ensure handle memory is freed
                handle_.destroy(); 
            }
        }

        virtual inline bool awaited() final { return awaited_; }

        virtual inline bool await_ready() final {
            // acquire the lock 
            if(!(this->locked())) { this->lock(); }

            // set awaited flag
            awaited_ = true;

            // call the ready code
            return this->on_ready();
        }

        /// called by awaitable's await_suspend()
        virtual inline void await_suspend(std::coroutine_handle<> h) final {
            if(h) {
                // assign the handle to our member
                handle_ = h; 
                // the current coroutine no longer manages the handle
                coroutine::local().release(); 
                // unlock the lock before coroutine::resume() returns to the 
                // caller
                this->unlock();
            } else {
                // block the calling thread using traditional mechanisms
                atp_ = detail::coroutine::this_thread::get(); 
                detail::coroutine::this_thread::block(*this);
            }
        }

        /**
         @brief unblock and resume a suspended operation 

         This should be called by a different coroutine or thread.

         Calling this method will allow the unblock the thread or suspended 
         coroutine (`co_await` will return to its caller). 

         @param m arbitary memory passed to on_resume()
         */
        virtual inline void resume(void* m) final {
            // re-acquire the lock 
            if(!(this->locked())) { this->lock(); }

            // call the custom resumption code
            this->on_resume(m);

            if(atp_) { 
                // unblock the suspended thread
                atp_->unblock(*this); 
            } else { 
                // unblock the suspended coroutine and push the handle to its 
                // destination
                this->unlock();
                this->destination(handle_);
                handle_ = std::coroutine_handle<>();
            }
        }

        /// return true if already locked, else false
        virtual bool locked() = 0;

        /// acquire the awaitable's lock
        virtual void lock() = 0;

        /// release the awaitable's lock
        virtual void unlock() = 0;

        /// destination for coroutine_handle on resume
        virtual void destination(std::coroutine_handle<>) = 0;

        /**
         @brief returns whether the operation can resume immediately or not 

         This operation is where any code which needs to execute before suspend 
         should be called. The lock will be held while calling this method.
         */
        virtual bool on_ready() = 0;

        /**
         User inner method called when resume()ing the suspended operation.  

         It is passed whatever arbitary memory is passed to resume(). The 
         implementation may use this memory to complete the operation, in 
         whatever manner it sees fit. The lock will be held while calling this 
         method.

         Implementations must be aware of the fact that `on_resume()` will 
         be called with a `nullptr` by the implementation's destructor if 
         `on_resume()` was never called.

         @param m arbitrary memory
         */
        virtual void on_resume(void* m) = 0;

    private:
        bool awaited_ = false;
        detail::coroutine::this_thread* atp_ = nullptr;
        std::coroutine_handle<> handle_;
    };

    /**
     @brief partial implementation of awaitable::interface for a templated LOCK
     */
    template <typename INTERFACE, typename LOCK>
    struct lockable : public INTERFACE {
        template <typename... As>
        lockable(LOCK& lk, bool locked, As&&... as) : 
            INTERFACE(std::forward<As>(as)...),
            lk_(&lk), 
            locked_(locked) 
        { }

        /// ensure lock is unlocked when we go out of scope
        virtual ~lockable() { unlock(); }

        inline bool locked() final { return locked_; }

        /// acquire the lock
        inline void lock() final { 
            lk_->lock(); 
            locked_ = true;
        }

        /// release the lock
        inline void unlock() final { 
            lk_->unlock(); 
            locked_ = false;
        }

    private:
        LOCK* lk_;
        bool locked_;
    };

    /**
     @brief lockfree partial implementation of awaitable::interface
     */
    template <typename INTERFACE>
    struct lockfree : public INTERFACE {
        template <typename... As>
        lockfree(As&&... as) : 
            INTERFACE(std::forward<As>(as)...)
        { }

        virtual ~lockfree() { }
        inline bool locked() final { return false; }
        inline void lock() final { }
        inline void unlock() final { }
    };

    awaitable() = delete;
    awaitable(const awaitable&) = delete;
    awaitable(awaitable&&) = default;
    inline awaitable& operator=(const awaitable&) = delete;
    inline awaitable& operator=(awaitable&&) = default;

    /**
     @brief construct a awaitable with some awaitable::implementation 

     awaitables act like an `std::unique_ptr` when they are created with a 
     pointer to an implementation, they will delete their implementation when 
     they go out of scope.
     */
    template <typename IMPLEMENTATION>
    awaitable(IMPLEMENTATION* i) : 
        impl_(
            std::unique_ptr<awaitable::interface>(
                static_cast<awaitable::interface*>(i)))
    { }
    
    virtual inline ~awaitable() { if(impl_) { finalize(); } }

    /// swap awaitables
    inline void swap(awaitable& rhs) noexcept { std::swap(impl_, rhs.impl_); }

    /// return true if the awaitable has an implementation
    inline operator bool() { return (bool)impl_; }

    /// return a reference to the implementation
    inline interface& implementation() { return *impl_; }

    /**
     Immediately called by `co_await` keyword. If it returns `true`, 
     `await_ready()` is immediately called.
     */
    inline bool await_ready() { return impl_->await_ready(); }

    /**
     When using the `co_await` keyword, if `await_ready() == false`, this 
     function is called by the compiler with a new coroutine handler capable 
     of resuming where we suspended.

     Store the new coroutine handle so we can resume later, and call the 
     `suspend` operation.

     If the argument handle does not represent a coroutine (`handle == false`), 
     then the operation is on a system thread and will block the calling thread.
     */
    inline void await_suspend(std::coroutine_handle<> h) { impl_->await_suspend(h); }

    // ensure logic completes before awaitable goes out of scope
    inline void finalize() {
        if(!(impl_->awaited())) {
            if(coroutine::in()) { 
                // coroutine failed to `co_await` the awaitable
                detail::coroutine::coroutine_did_not_co_await(this); 
            } else if(!await_ready()) { 
                // if we're here, this awaitable is operating without the 
                // `co_await` keyword, and needs to operate as a regular system 
                // thread blocking call, not a coroutine suspend.
                await_suspend(std::coroutine_handle<>()); 
            }
        } 
    }

private:
    std::unique_ptr<interface> impl_;
};

/**
 @brief partial implementation for stackless awaitables used by this library 

 This is the type that should be inheritted by descendant implementations.

 If used by non-coroutines then converting/casting this object to its templated 
 type `T` or the object being destroyed will block the thread until the 
 operation completes. 

 Likewise, when a coroutine `co_await`s on this object it will suspend until the 
 operation completes. If a coroutine fails to `co_await` the object an exception 
 will be thrown.

 The result of the awaitable operation is of type `T`, which will be returned 
 to a coroutine from the `co_await` expression or can be converted directly to 
 type `T` by a non-coroutine.

 Given function `awt<T> my_operation()`:

 coroutine:
 ```
 T my_t = co_await my_operation();
 ```

 non-coroutine:
 ```
 T my_t = my_operation();
 ```
 */
template <typename T>
struct awt : public awaitable {
    // the necessary operations required by an instance of awaitable<T>
    struct interface : public awaitable::interface {
        // ensure destructor is virtual to properly destruct
        virtual inline ~interface() { }

        ///return the final result of the `awaitable<T>`
        virtual T get_result() = 0;
    };

    typedef T value_type;

    awt() = delete;
    awt(const awt<T>& rhs) = delete;
    awt(awt<T>&& rhs) = default;

    template <typename IMPLEMENTATION>
    awt(IMPLEMENTATION* i) : 
        awaitable(
            // must be able to cast the implementation to the required parent type
            dynamic_cast<awt<T>::interface*>(i)) 
    { }
    
    inline awt& operator=(const awt<T>& rhs) = delete;
    inline awt& operator=(awt<T>&& rhs) = default;

    virtual ~awt(){ }

    /**
     Return the final result of `awt<T>`
     */
    inline T await_resume(){ 
        return dynamic_cast<awt<T>::interface&>(this->implementation()).get_result();
    }

    /**
     Inline conversion for use in standard threads where `co_await` isn't called.
     */
    inline operator T() {
        this->finalize(); 
        return await_resume();
    }
};

/**
 @brief partial template specialization for awaitable with no return value 

 Coroutines must `co_await` this as usual to block (non-coroutines will block 
 when the object goes out of scope). However, no value will be returned by the 
 awaitable on resumption.

 Given function `awt<void> my_operation()`:

 coroutine:
 ```
 co_await my_operation();
 ```

 non-coroutine:
 ```
 my_operation();
 ```
 */
template <>
struct awt<void> : public awaitable {
    struct interface : public awaitable::interface {
        virtual inline ~interface() { }
    };

    typedef void value_type;

    awt() = delete;
    awt(const awt<void>& rhs) = delete;
    awt(awt<void>&& rhs) = default;

    template <typename IMPLEMENTATION>
    awt(IMPLEMENTATION* i) : 
        awaitable(
            dynamic_cast<awt<void>::interface*>(i)) 
    { }

    virtual inline ~awt() { }
    
    inline awt& operator=(const awt<void>& rhs) = delete;
    inline awt& operator=(awt<void>&& rhs) = default;

    inline void await_resume(){ }
};

}

#endif
