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
#include <string>
#include <sstream>

// local 
#include "atomic.hpp"

namespace hce {

/** 
 @brief interface coroutine type 

 An actual coroutine must be an implementation of coroutine<T> in order to 
 have a valid promise_type.
 */
struct base_coroutine {
    typedef std::function<void(std::coroutine_handle<>)> destination;

    /**
     The base promise object for stackless coroutines, used primarily by the 
     compiler when it constructs stackless coroutines.
     */
    struct promise_type {
        virtual ~promise_type() { }

        inline base_coroutine get_return_object() {
            return { 
                std::coroutine_handle<promise_type>::from_promise(*this) 
            };
        }

        inline std::suspend_always initial_suspend() { return {}; }
        inline std::suspend_always final_suspend() noexcept { return {}; }
        inline void unhandled_exception() { eptr = std::current_exception(); }

        /// return actual coroutine<T>::promise_type type
        virtual const std::type_info& type_info() = 0;

        std::exception_ptr eptr = nullptr; // exception pointer
    };

    base_coroutine() = default;
    base_coroutine(const base_coroutine&) = delete;
    base_coroutine(base_coroutine&& rhs) = default;

    // construct the base_coroutine from a type erased handle
    base_coroutine(std::coroutine_handle<> h) : handle_(std::move(h)) { }

    base_coroutine& operator=(const base_coroutine&) = delete;
    base_coroutine& operator=(base_coroutine&& rhs) = default;

    virtual ~base_coroutine() { if(handle_) { handle_.destroy(); } }

    /// return true if the handle is valid, else false
    inline operator bool() const { return (bool)handle_; }

    /// cease managing the current handle and return it
    inline std::coroutine_handle<> release() noexcept {
        auto hdl = handle_;
        handle_ = std::coroutine_handle<>();
        return hdl;
    }

    /// replace the coroutine's stored handle
    inline void reset(std::coroutine_handle<> h) noexcept { 
        if(handle_) { handle_.destroy(); }
        handle_ = h; 
    }

    /// swap coroutines 
    inline void swap(base_coroutine& rhs) noexcept { 
        auto handle = handle_;
        handle_ = rhs.handle_;
        rhs.handle_ = handle;
    }

    /// return true if the coroutine is done, else false
    inline bool done() const { return handle_.done(); }

    /**
     @brief cast the base_coroutine to a descendant type 

     Will need to call this on a base_coroutine to get the actual promise. When 
     expecting coroutine<T>:
     ```
     base_coroutine my_co;
     coroutine<T>::promise& my_promise = my_co.to<coroutine<T>>().promise();
     ```
     */
    template <typename COROUTINE>
    inline COROUTINE& to() {
        return dynamic_cast<COROUTINE&>(*this);
    }
   
    /// return true if we are in a coroutine, else false
    static inline bool in() { 
        return base_coroutine::tl_this_coroutine(); 
    }

    /// return the coroutine running on this thread
    static inline base_coroutine& local() { 
        return *(base_coroutine::tl_this_coroutine()); 
    }

    /// resume the coroutine 
    inline void resume() {
        auto& tl_co = base_coroutine::tl_this_coroutine();

        // store parent coroutine pointer
        auto parent_co = tl_co;

        // set current coroutine ptr 
        tl_co = this; 
        
        // continue coroutine execution
        try {
            handle_.resume();

            // acquire the exception pointer
            auto eptr = std::coroutine_handle<promise_type>::from_address(
                handle_.address()).promise().eptr;

            // rethrow any exceptions from the coroutine
            if(eptr) { std::rethrow_exception(eptr); }
        } catch(...) {
            // restore parent coroutine ptr
            tl_co = parent_co;
            std::rethrow_exception(std::current_exception());
        }
    }

    /// convert the base_coroutine to the underlying handle by move
    inline operator std::coroutine_handle<>() { return std::move(handle_); }

protected:
    // always points to the coroutine running on the scheduler on this thread
    static base_coroutine*& tl_this_coroutine();

    // the coroutine's managed handle
    std::coroutine_handle<> handle_;
};

/** 
 @brief stackless management coroutine object

 User stackless coroutine implementations must return this object. 

 `hce::coroutine` act like `std::unique_ptr`s for `std::coroutine_handle<>`s.
 If an `hce::coroutine` owns a valid handle when it goes out of scope it will 
 call the handle's `destroy()` operation.
 */
template <typename T>
struct coroutine : public base_coroutine {
    struct promise_type : public base_coroutine::promise_type {
        /**
         Object responsible for handling cleanup prior to the promise going out 
         of scope.
         */
        struct cleanup {
            typedef std::function<void(promise_type*)> handler;

            cleanup(promise_type* pt) : promise_(pt) {}
            ~cleanup() { for(auto& h : handlers_) { h(promise_); } }

            inline void install(handler&& h) { 
                handlers_.push_back(std::move(h)); 
            }

        private:    
            promise_type* promise_;
            std::deque<handler> handlers_; 
        };

        /// ensure cleanup handlers are run before promise_type destructs
        virtual ~promise_type(){ if(cleanup_) { delete cleanup_; } }

        inline const std::type_info& type_info() { 
            return typeid(coroutine<T>::promise_type); 
        }

        /**
         @brief store the result of `co_return` 

         Use a new template type to prevent template type shadowing and retain 
         universal reference semantics.
         */ 
        template <typename T2>
        inline void return_value(T2&& t) {
            result = std::forward<T2>(t);
        }

        /// install a coroutine<T>::promise_type::cleanup::handler
        inline void install(cleanup::handler hdl) {
            if(!cleanup_) { cleanup_ = new cleanup(this); }
            cleanup_->install(std::move(hdl));
        }

        /// the result of the coroutine<T>
        T result; 

    private: 
        cleanup* cleanup_ = nullptr;
    };

    typedef std::coroutine_handle<promise_type> handle_type;

    /// return the promise associated with this coroutine's handle
    inline promise_type& promise() const {
        return handle_type::from_address(handle_.address()).promise();
    }
    
    coroutine() = default;
    coroutine(const coroutine<T>&) = delete;
    coroutine(coroutine<T>&& rhs) = default;
    
    coroutine(const base_coroutine&) = delete;

    coroutine(base_coroutine&& rhs) : 
        base_coroutine(std::move(rhs))
    { }

    // construct the coroutine from a type erased handle
    coroutine(std::coroutine_handle<> h) : base_coroutine(std::move(h)) { }

    coroutine<T>& operator=(const coroutine<T>&) = delete;
    coroutine<T>& operator=(coroutine<T>&& rhs) = default;
    coroutine<T>& operator=(const base_coroutine&) = delete;

    coroutine<T>& operator=(base_coroutine&& rhs) {
        handle_ = coroutine<T>(std::move(rhs));
        return *this;
    }
};

template <>
struct coroutine<void> : public base_coroutine {
    struct promise_type : public base_coroutine::promise_type {
        /**
         Object responsible for handling cleanup prior to the promise going out 
         of scope.
         */
        struct cleanup {
            typedef std::function<void(promise_type*)> handler;

            cleanup(promise_type* pt) : promise_(pt) {}
            ~cleanup() { for(auto& h : handlers_) { h(promise_); } }

            inline void install(handler&& h) { 
                handlers_.push_back(std::move(h)); 
            }

        private:    
            promise_type* promise_;
            std::deque<handler> handlers_; 
        };

        /// ensure cleanup handlers are run before promise_type destructs
        virtual ~promise_type(){ if(cleanup_) { delete cleanup_; } }

        inline const std::type_info& type_info() { 
            return typeid(coroutine<void>::promise_type); 
        }

        inline void return_void() {}

        /// install a coroutine<T>::promise_type::cleanup::handler
        inline void install(cleanup::handler hdl) {
            if(!cleanup_) { cleanup_ = new cleanup(this); }
            cleanup_->install(std::move(hdl));
        }

    private: 
        cleanup* cleanup_ = nullptr;
    };

    typedef std::coroutine_handle<promise_type> handle_type;

    /// return the promise associated with this coroutine's handle
    inline promise_type& promise() const {
        return handle_type::from_address(handle_.address()).promise();
    }
    
    coroutine() = default;
    coroutine(const coroutine<void>&) = delete;
    coroutine(coroutine<void>&& rhs) = default;
    
    coroutine(const base_coroutine&) = delete;

    coroutine(base_coroutine&& rhs) : 
        base_coroutine(std::move(rhs))
    { }

    // construct the coroutine from a type erased handle
    coroutine(std::coroutine_handle<> h) : base_coroutine(std::move(h)) { }

    coroutine<void>& operator=(const coroutine<void>&) = delete;
    coroutine<void>& operator=(coroutine<void>&& rhs) = default;
    coroutine<void>& operator=(const base_coroutine&) = delete;

    coroutine<void>& operator=(base_coroutine&& rhs) {
        handle_ = coroutine<void>(std::move(rhs));
        return *this;
    }
};

/// thread_local block/unblock functionality
struct this_thread {
    // get the this_thread object associated with the calling thread
    static this_thread* get();

    // can only block the calling thread
    template <typename LOCK>
    static inline void block(std::unique_lock<LOCK>& lk) {
        this_thread::get()->block_(lk);
    }

    // unblock an arbitrary this_thread
    template <typename LOCK>
    inline void unblock(std::unique_lock<LOCK>& lk) {
        ready = true;
        lk.unlock();
        cv.notify_one();
    }

private:
    this_thread() : ready{false} { }

    template <typename LOCK>
    inline void block_(std::unique_lock<LOCK>& lk) {
        while(!(ready)) { cv.wait(lk); }
        ready = false;
    }

    bool ready;
    std::condition_variable_any cv;
};

namespace detail {

void coroutine_did_not_co_await(void* awt);
void awaitable_not_resumed(void* awt, void* hdl);

}

/**
 @brief awaitables inherit shared functionality defined here

 base_awaitable objects cannot be used by themselves, they must be inheritted 
 by another object (awaitable<T>) which implements the `await_resume()` method.
 */
template <typename LOCK>
struct base_awaitable {
    struct implementation {
        virtual ~implementation() { 
            if(handle_) {
                detail::awaitable_not_resumed(this, handle_.address());

                // ensure handle memory is freed
                handle_.destroy(); 
            }
        }

        /**
         @brief unblock and resume a suspended operation 

         This should be called by a different coroutine or thread.

         Calling this method will allow the unblock the thread or suspended 
         coroutine (`co_await` will return to its caller). 

         @param m arbitary memory passed to implementation::resume()
         */
        inline void resume(void* m) {
            // acquire the reference to the lock, should be unlocked at this point
            std::unique_lock<LOCK>& lk = this->get_lock();

            // re-acquire the lock 
            lk.lock();
            this->resume_impl(m);

            if(atp_) { 
                // unblock the suspended thread
                atp_->unblock(lk); 
            } else { 
                // unblock the suspended coroutine
                lk.unlock();
                destination_(std::move(handle_));
            }
        }

    protected:
        /**
         Returns the LOCK used by the operation. 

         This object should be be locked by the time the implementation's 
         construction is finished.

         This lock will be unlocked by the awaitable during a suspend. 
         */
        virtual std::unique_lock<LOCK>& get_lock() = 0;

        /**
         @brief returns whether the operation can resume immediately or not 

         This operation is where any code which needs to execute before suspend 
         should be called. The lock will be held while calling this method.
         */
        virtual bool ready_impl() = 0;

        /**
         User inner method called when resume()ing the suspended operation.  

         It is passed whatever arbitary memory is passed to resume(). The 
         implementation may use this memory to complete the operation, in 
         whatever manner it sees fit. The lock will be held while calling this 
         method.

         Implementations must be aware of the fact that `resume_impl()` will 
         be called with a `nullptr` by the implementation's destructor if 
         `resume_impl()` was never called.

         @param m arbitrary memory
         */
        virtual void resume_impl(void* m) = 0;

        /**
         @brief method to acquire a callback to deal with the handle when resumed 

         This method is not called when the awaitable is being used outside a
         coroutine.
         */
        virtual base_coroutine::destination acquire_destination() = 0;

    private:
        /// called by awaitable's await_suspend()
        inline void suspend_(std::coroutine_handle<> h) {
            std::unique_lock<LOCK>& lk = this->get_lock();

            if(h) {
                handle_ = h; 
                destination_ = acquire_destination();
                base_coroutine::local().reset(std::coroutine_handle<>());
                lk.unlock();
            } else {
                atp_ = this_thread::get(); 
                this_thread::block(lk);
            }
        }

        this_thread* atp_ = nullptr;
        std::coroutine_handle<> handle_;
        base_coroutine::destination destination_;

        friend struct base_awaitable<LOCK>;
    };

    base_awaitable() = delete;
    base_awaitable(const base_awaitable<LOCK>&) = delete;
    base_awaitable(base_awaitable<LOCK>&&) = default;
    base_awaitable<LOCK>& operator=(const base_awaitable<LOCK>&) = delete;
    base_awaitable<LOCK>& operator=(base_awaitable<LOCK>&&) = default;

    /**
     @brief construct a base_awaitable with some base_awaitable::implementation 

     base_awaitables act like an `std::unique_ptr` when they are created with a 
     pointer to an implementation, they will delete their implementation when 
     they go out of scope.
     */
    template <typename IMPLEMENTATION>
    base_awaitable(IMPLEMENTATION* i) : 
        impl_(static_cast<base_awaitable<LOCK>::implementation*>(i))
    { }
    
    virtual inline ~base_awaitable() {
        if(impl_) {
            finalize_(); 
            delete impl_;
        }
    }

    /**
     Immediately called by `co_await` keyword. If it returns `true`, 
     `await_ready()` is immediately called.
     */
    inline bool await_ready() {
        awaited_ = true;
        return impl_->ready_impl();
    }

    /**
     When using the `co_await` keyword, if `await_ready() == false`, this 
     function is called by the compiler with a new coroutine handler capable 
     of resuming where we suspended.

     Store the new coroutine handle so we can resume later, and call the 
     `suspend` operation.

     If the argument handle does not represent a coroutine (`handle == false`), 
     then the operation is on a system thread and will block the calling thread.
     */
    inline void await_suspend(std::coroutine_handle<> h) { impl_->suspend_(h); }

    /// return true if the awaitable has an implementation
    inline operator bool() { return impl_; }

    /// return a reference to the implementation
    inline implementation& impl() { return *impl_; }

    /// destroy the implementation, preventing finalization on destructor
    inline void reset() { if(impl_) { delete impl_; } }

    /// swap base_awaitables
    inline void swap(base_awaitable& rhs) noexcept { 
        auto impl = impl_;
        impl_ = rhs.impl_;
        rhs.impl_ = impl;
    }

    /// release the implementation to the caller
    inline implementation* release() { 
        auto impl = impl_;
        impl_ = nullptr;
        return impl;
    }

private:
    inline void finalize_() {
        if(!awaited_) {
            if(base_coroutine::in()) { 
                // coroutine failed to `co_await` the awaitable
                detail::coroutine_did_not_co_await(this); 
            } else if(!await_ready()) { 
                // if we're here, this awaitable is operating without the 
                // `co_await` keyword, and needs to operate as a regular system 
                // thread blocking call, not a coroutine suspend.
                await_suspend(std::coroutine_handle<>()); 
            }
        } 
    }

    implementation* impl_ = nullptr;
    bool awaited_ = false;
};

/**
 @brief implementation for stackless awaitables used by this library 

 If used by non-coroutines then converting this object to its templated type `T`
 or the object being destroyed will block the thread until the operation 
 completes.

 Likewise, when a coroutine `co_await`s on this object it will suspend until the 
 operation completes. If a coroutine fails to `co_await` the object an exception 
 will be thrown.

 The result of the awaitable operation is of type `T`, which will be returned 
 to a coroutine from the `co_await` expression or can be converted directly to 
 type `T` by a non-coroutine.

 coroutine:
 ```
 T my_t = co_await operation_returning_awaitable();
 ```

 non-coroutine:
 ```
 T my_t = operation_returning_awaitable();
 ```
 */
template <typename T, typename LOCK=spinlock>
struct awaitable : public base_awaitable<LOCK> {
    // the necessary operations required by an instance of awaitable<T>
    struct implementation : public base_awaitable<LOCK>::implementation {
        // ensure destructor is virtual to properly deconstruct
        virtual inline ~implementation() { }

    protected:
        /**
         @return the final result of the `awaitable<T>`
         */
        virtual T result_impl() = 0;

        friend struct awaitable<T,LOCK>;
    };

    typedef T value_type;

    awaitable() = delete;
    awaitable(const awaitable<T,LOCK>& rhs) = delete;
    awaitable(awaitable<T,LOCK>&& rhs) = default;

    template <typename IMPLEMENTATION>
    awaitable(IMPLEMENTATION* i) : 
        base_awaitable<LOCK>(
            // must be able to cast the implementation to the required parent type
            dynamic_cast<awaitable<T,LOCK>::implementation*>(i)) 
    { }
    
    awaitable& operator=(const awaitable<T,LOCK>& rhs) = delete;
    awaitable& operator=(awaitable<T,LOCK>&& rhs) = default;

    virtual ~awaitable(){ }

    /**
     Return the final result of `awaitable<T>`
     */
    inline T await_resume(){ 
        return dynamic_cast<awaitable<T,LOCK>::implementation&>(
            this->impl()).result_impl();
    }

    /**
     Inline conversion for use in standard threads where `co_await` isn't called.
     */
    inline operator T() {
        this->finalize_(); 
        return await_resume();
    }
};

/**
 @brief full template specialization for awaitable with no return value 

 Coroutines must `co_await` this as usual to block (non-coroutines will block 
 when the object goes out of scope). However, no value will be returned by the 
 awaitable on resumption.
 */
template <typename LOCK>
struct awaitable<void,LOCK> : public base_awaitable<LOCK> {
    struct implementation : public base_awaitable<LOCK>::implementation {
        virtual inline ~implementation() { }
    };

    typedef void value_type;

    awaitable() = delete;
    awaitable(const awaitable<void,LOCK>& rhs) = delete;
    awaitable(awaitable<void,LOCK>&& rhs) = default;

    template <typename IMPLEMENTATION>
    awaitable(IMPLEMENTATION* i) : 
        base_awaitable<LOCK>(
            dynamic_cast<awaitable<void,LOCK>::implementation*>(i)) 
    { }
    
    awaitable& operator=(const awaitable<void,LOCK>& rhs) = delete;
    awaitable& operator=(awaitable<void,LOCK>&& rhs) = default;

    virtual inline ~awaitable() { }
    inline void await_resume(){ }
};

struct base_yield {
    ~base_yield() {
        if(base_coroutine::in() && !awaited_){ 
            detail::coroutine_did_not_co_await(this); 
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

/**
 @brief `co_await` to suspend execution and allow other coroutines to run.

 `coroutine::done()` will `== false` after suspending in this 
 manner, so the coroutine can be requeued immediately.

 `yield<T>` returns a value `T` which is returned on resumption.

 If used by non-coroutines no suspend will occur, and the value is returned 
 immediately.
 */
template <typename T>
struct yield : public base_yield {
    template <typename... As>
    yield(As&&... as) : t(std::forward<As>(as)...) { }

    inline T await_resume() { return std::move(t); }

    inline operator T() {
        if(base_coroutine::in()) { detail::coroutine_did_not_co_await(this); }
        return await_resume(); 
    }

private:
    bool awaited_ = false;
    T t;
};

/// full void specialization
template <>
struct yield<void> : public base_yield {
    inline void await_resume() { }
};

}

#endif
