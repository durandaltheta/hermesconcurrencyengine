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
#include <iostream>

// local 
#include "atomic.hpp"

namespace hce {

// forward declarations
struct coroutine;

namespace detail {

// always points to the running coroutine
coroutine*& tl_this_coroutine();

}

/// Returns true if executing in a coroutine, else false
inline bool in_coroutine() {
    return detail::tl_this_coroutine() ? true : false; 
}

/// return the currently running coroutine 
inline coroutine& this_coroutine() {
    return *(detail::tl_this_coroutine()); 
}

/** 
 @brief stackless management coroutine object

 User stackless coroutine implementations must return this object.
 */
struct coroutine {
    struct promise_type;
    typedef std::coroutine_handle<promise_type> handle_type;
        
    typedef std::function<void(std::coroutine_handle<>&&)> destination;

    /**
     The promise object for stackless coroutines, used primarily by the compiler 
     when it constructs stackless coroutines.
     */
    struct promise_type {
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

        ~promise_type() { 
            // ensure cleanup handlers execute before promise_type is destroyed
            cleanup_.reset(); 
        }

        inline coroutine get_return_object() {
            return { handle_type::from_promise(*this) };
        }

        inline std::suspend_always initial_suspend() { return {}; }
        inline std::suspend_always final_suspend() noexcept { return {}; }
        inline void return_void() {}

        // store the arbitrary result of `co_return`
        template <typename T>
        inline void return_value(T&& t) {
            result = std::forward<T>(t);
        }

        inline void unhandled_exception() { eptr = std::current_exception(); }
       
        // install a cleanup handler to be executed when the coroutine promise 
        // is destroyed (when the handle is destroyed)
        inline void install(cleanup::handler h) {
            if(!cleanup_) { 
                cleanup_ = std::unique_ptr<cleanup>(new cleanup(this));
            }

            cleanup_->install(std::move(h));
        }

        std::exception_ptr eptr = nullptr; // exception pointer
        std::any result; // potential `co_return`ed result  

    private:
        std::unique_ptr<cleanup> cleanup_;
    };
    
    coroutine() = default;
    coroutine(const coroutine&) = delete;
    coroutine(coroutine&& rhs) = default;

    // construct the coroutine from a type erased handle
    coroutine(std::coroutine_handle<> h) : handle_(std::move(h)) { }

    virtual ~coroutine() { if(handle_) { handle_.destroy(); } }

    coroutine& operator=(const coroutine&) = delete;
    coroutine& operator=(coroutine&& rhs) = default;

    /// return true if the handle is valid, else false
    inline operator bool() { return (bool)handle_; }

    /// return true if the coroutine is done, else false
    inline bool done() { return handle_.done(); }

    /**
     This reference can be moved if necessary, allowing control of the handle 
     to transfer to a new owning coroutine.

     @return a reference to the coroutine's handle
     */
    inline std::coroutine_handle<>& handle() { return handle_; }

    /// return the promise associated with this coroutine's handle
    inline promise_type& promise() {
        return handle_type::from_address(handle_.address()).promise();
    }

    /*
     Execute until thunk completes or yield() is called 

     Execute until thunk completes or yield() is called. If coroutine handle is 
     complete resume() returns immediately.
    */
    inline void resume() {
        if(!done()) {
            auto& tl_co = detail::tl_this_coroutine();

            // store parent coroutine pointer
            auto parent_co = tl_co;

            // set current coroutine ptr 
            tl_co = this; 
            
            // continue coroutine execution
            try {
                handle_.resume();

                // acquire the exception pointer
                auto eptr = promise().eptr;

                // rethrow any exceptions from the coroutine
                if(eptr) { std::rethrow_exception(eptr); }
            } catch(...) {
                // restore parent coroutine ptr
                tl_co = parent_co;
                std::rethrow_exception(std::current_exception());
            }

            // restore parent coroutine ptr
            tl_co = parent_co;
        };
    }

private:
    std::coroutine_handle<> handle_;
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

inline void coroutine_did_not_co_await(void* awt) {
    std::stringstream ss;
    ss << "coroutine[0x" 
       << (void*)&this_coroutine() 
       << "] did not call co_await on an awaitable[0x"
       << awt 
       << "]";
    std::cerr << ss.str() << std::endl;
    std::terminate();
}

// awaitables inherit shared functionality defined
template <typename LOCK>
struct base_awaitable {
    struct implementation {
        struct awaitable_not_resumed : public std::exception {
            awaitable_not_resumed(implementation* addr) : 
                estr([=]() -> std::string {
                    std::stringstream ss;
                    ss << "awaitable[0x" 
                       << (void*)addr
                       << "] was not resumed before being destroyed";
                    return ss.str();
                }()) 
            { }

            const char* what() const { return estr.c_str(); }

        private:
            const std::string estr;
        };

        virtual ~implementation() { 
            if(!resumed_) { throw awaitable_not_resumed(this); }
        }

        /**
         @brief unblock and resume a suspended operation 

         This should be called by a different coroutine or thread.

         Calling this method will allow the unblock the thread or suspended 
         coroutine (`co_await` will return to its caller). 

         @param m arbitary memory passed to implementation::resume()
         */
        inline void resume(void* m) {
            std::unique_lock<LOCK>& lk = this->get_lock();
            resume_(lk, m);
        }

    protected:
        /**
         Returns a locked LOCK used by the operation. This lock will be 
         unlocked by the awaitable during suspend. This operation is called 
         when the awaitable is constructed from the implementation.

         It is held during calls to ready() and resume(), and unlocked when 
         await_suspend() is called.
         */
        virtual std::unique_lock<LOCK>& get_lock() = 0;

        /**
         @brief returns whether the operation can resume immediately or not 

         This operation is where any code which needs to execute before suspend 
         should be called.
         */
        virtual bool ready_impl() = 0;

        /**
         User inner method called when resume()ing the suspended operation.  

         It is passed whatever arbitary memory is passed to resume(). The 
         implementation may use this memory to complete the operation, in 
         whatever manner it sees fit.

         Implementations must be aware of the fact that `resume_impl()` will 
         be called with a `nullptr` by the implementation's destructor if 
         `resume_impl()` was never called.

         @param m arbitrary memory
         */
        virtual void resume_impl(void* m) = 0;

        /**
         @brief method to acquire a callback to deal with the handle when resumed
         */
        virtual coroutine::destination acquire_destination() = 0;

    private:
        /// called by awaitable's await_suspend()
        inline void suspend(std::coroutine_handle<> h) {
            std::unique_lock<LOCK>& lk = this->get_lock();

            if(h) {
                handle_ = h; 
                destination_ = acquire_destination();
                this_coroutine().handle() = std::coroutine_handle<>();
                lk.unlock();
            } else {
                atp_ = this_thread::get(); 
                this_thread::block(lk);
            }
        }

        inline void resume_(std::unique_lock<LOCK>& lk, void* m) {
            resumed_ = true;

            // acquire the lock 
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

        bool resumed_ = false;
        this_thread* atp_ = nullptr;
        std::coroutine_handle<> handle_;
        coroutine::destination destination_;

        friend struct base_awaitable<LOCK>;
    };

    // awaitable is a transient object, it must be used inline
    base_awaitable() = delete;
    base_awaitable(const base_awaitable<LOCK>&) = delete;
    base_awaitable(base_awaitable<LOCK>&&) = default;
    base_awaitable<LOCK>& operator=(const base_awaitable<LOCK>&) = delete;
    base_awaitable<LOCK>& operator=(base_awaitable<LOCK>&&) = default;

    /**
     @brief construct a base_awaitable with some base_awaitable::implementation
     */
    template <typename IMPLEMENTATION>
    base_awaitable(std::unique_ptr<IMPLEMENTATION> i) : 
        impl_(static_cast<base_awaitable<LOCK>::implementation*>(i.release()))
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
    inline void await_suspend(std::coroutine_handle<> h) { impl_->suspend(h); }

    inline implementation& get_impl() { return *impl_; }

private:
    inline void finalize_() {
        if(!awaited_) {
            if(in_coroutine()) { 
                // coroutine failed to `co_await` the awaitable
                coroutine_did_not_co_await(this); 
            } else if(!await_ready()) { 
                // if we're here, this awaitable is operating without the 
                // `co_await` keyword, and needs to operate as a regular system 
                // thread blocking call, not a coroutine suspend.
                await_suspend(std::coroutine_handle<>()); 
            }
        } 
    }

    implementation* impl_;
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
    struct implementation : protected base_awaitable<LOCK>::implementation {
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

    awaitable(std::unique_ptr<implementation> i) : 
        base_awaitable<LOCK>(std::move(i)) 
    { }

    virtual ~awaitable(){ }

    // construct with an an allocated implementation
    template <typename IMPLEMENTATION, typename... As>
    static awaitable<bool> make(As&&... as) 
    {
        return {
            std::unique_ptr<implementation>(
                static_cast<implementation*>(
                    new IMPLEMENTATION(std::forward<As>(as)...)))
        };
    }

    /**
     Return the final result of `awaitable<T>`
     */
    inline T await_resume(){ 
        return dynamic_cast<awaitable<T,LOCK>::implementation&>(
            this->get_impl()).result_impl();
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
    struct implementation : protected base_awaitable<LOCK>::implementation {
        virtual inline ~implementation() { }
    };

    typedef void value_type;

    awaitable(std::unique_ptr<implementation> i) : 
        base_awaitable<LOCK>(std::move(i)) { }

    // construct with an an allocated implementation
    template <typename IMPLEMENTATION, typename... As>
    static awaitable<void> make(As&&... as) {
        return {
            std::unique_ptr<implementation>(
                static_cast<implementation*>(
                    new IMPLEMENTATION(std::forward<As>(as)...)))
        };
    }

    virtual inline ~awaitable() { }
    inline void await_resume(){ }
};

struct base_yield {
    ~base_yield() {
        if(in_coroutine() && !awaited_){ 
            coroutine_did_not_co_await(this); 
        }
    }

    inline bool await_ready() {
        awaited_ = true;
        return false; 
    }

    inline void await_suspend(std::coroutine_handle<> h) {
        this_coroutine().handle() = std::move(h);
    }

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
        if(in_coroutine()) { coroutine_did_not_co_await(this); }
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
