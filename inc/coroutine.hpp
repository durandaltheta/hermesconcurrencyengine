//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#ifndef HERMES_COROUTINE_ENGINE_COROUTINE
#define HERMES_COROUTINE_ENGINE_COROUTINE

// c++
#include <memory>
#include <mutex>
#include <condition_variable>
#include <coroutine>
#include <thread>
#include <exception>
#include <string>
#include <ostream>
#include <sstream>
#include <functional>

// local 
#include "utility.hpp"
#include "logging.hpp"
#include "memory.hpp"
#include "alloc.hpp"
#include "chrono.hpp"

namespace hce {

struct coroutine;

namespace detail {
namespace coroutine {

// always points to the coroutine running on this thread
hce::coroutine*& tl_this_coroutine();

}
}


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
    struct promise_type : public printable {
        /// This object contains the necessary values for the cleanup handler 
        struct cleanup_data {
            void* install; /// arg pointer provided to install()
            void* promise; /// pointer to the descendent promise_type instance
        };

        /// a function pointer to a cleanup operation
        using cleanup_operation = void (*)(cleanup_data&);

        promise_type() { }
        virtual ~promise_type(){ }

        // ensure the use of non-throwing operator-new
        static coroutine get_return_object_on_allocation_failure() {
            throw std::bad_alloc();
            return coroutine(nullptr);
        }

        /**
         @brief implement custom new to make use of thread local allocation caching
         */
        inline void* operator new(std::size_t n) noexcept {
            return hce::memory::allocate(n);
        }

        /**
         @brief implement custom delete to make use of thread local allocation caching 
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
        inline void unhandled_exception() { eptr = std::current_exception(); }

        /// exception pointer to the most recently raised exception
        std::exception_ptr eptr = nullptr; 

        /**
         @brief install a cleanup operation 

         Cleanup handlers are installed as a list. Installed handlers are 
         executed FILO (first in, last out).

         @param co a cleanup operation function pointer 
         @param arg some arbitrary data to be passed to `co` in the `cleanup_data` struct
         */
        inline void install(cleanup_operation co, void* arg) {
            HCE_LOW_METHOD_ENTER("install", reinterpret_cast<void*>(co), arg);

            node* next = hce::allocate<node>(1);
            new(next) node(cleanup_list_, co, arg);
            cleanup_list_ = next;
        }

        /**
         @brief execute any installed callback operations

         This should be called by the hce::co<T>::promise_type destructor. It 
         cannot be called by the underlying `hce::coroutine::promise_type` 
         destructor because cleanup handlers may need to access members of the 
         descendent type.
         */
        inline void cleanup() {
            // trigger the callback if it is set, then unset it
            if(cleanup_list_) [[likely]] {
                HCE_LOW_METHOD_BODY("cleanup", "calling operations");

                do {
                    HCE_MIN_METHOD_BODY(
                        "cleanup", 
                        reinterpret_cast<void*>(cleanup_list_->co), 
                        cleanup_list_->install, 
                        (void*)this);

                    cleanup_data d{cleanup_list_->install, this};
                    cleanup_list_->co(d);
                    node* old = cleanup_list_;
                    cleanup_list_ = cleanup_list_->next;
                    hce::deallocate(old);
                } while(cleanup_list_);
            } else {
                HCE_LOW_METHOD_BODY("cleanup", "no cleanup operation");
            }
        }

    private:
        /// object for creating a simple singly linked list of cleanup handlers
        struct node {
            node* next; /// the next node in the cleanup handler list
            cleanup_operation co; /// the cleanup operation provided to install()
            void* install; /// data pointer provied to install()
        };

        node* cleanup_list_ = nullptr; // list of cleanup handlers
    };

    coroutine() { }
    coroutine(const coroutine&) = delete;

    coroutine(coroutine&& rhs) {
        HCE_MED_GUARD(rhs.handle_, HCE_MED_CONSTRUCTOR(rhs)); 
        swap(rhs); 
    }

    // construct the coroutine from a type erased handle
    coroutine(std::coroutine_handle<>&& h) : handle_(h) { 
        HCE_MIN_GUARD(h,HCE_MIN_CONSTRUCTOR(h));

        // ensure source memory no longer has access to the handle
        h = std::coroutine_handle<>();
    }

    inline coroutine& operator=(const coroutine&) = delete;

    inline coroutine& operator=(coroutine&& rhs) { 
        HCE_MED_METHOD_ENTER("operator=",rhs);
        swap(rhs);
        return *this;
    }

    virtual ~coroutine() {
        HCE_MED_GUARD(handle_,HCE_MED_DESTRUCTOR()); 
        reset(); 
    }

    static inline std::string info_name() { return "hce::coroutine"; }
    inline std::string name() const { return coroutine::info_name(); }

    /// return our stringified coroutine handle's address
    inline std::string content() const { 
        if(handle_) {
            std::stringstream ss;
            ss << handle_;
            return ss.str();
        } else { return std::string(); }
    }

    /// return true if the handle is valid, else false
    inline operator bool() const { return (bool)handle_; }

    /// releases ownership of the managed handle and returns it
    inline std::coroutine_handle<> release() {
        HCE_LOW_METHOD_ENTER("release");
        auto h = handle_;
        handle_ = std::coroutine_handle<>();
        return h;
    }

    /// cleans up and resets the managed handle
    inline void reset() { 
        HCE_TRACE_METHOD_ENTER("reset");
        if(handle_) [[likely]] { destroy_(); }
        handle_ = std::coroutine_handle<>(); 
    }

    /// cleans up and replaces the managed handle
    inline void reset(std::coroutine_handle<> h) { 
        HCE_TRACE_METHOD_ENTER("reset", h);
        if(handle_) [[likely]] { destroy_(); }
        handle_ = h; 
    }

    /// swap two coroutines
    inline void swap(coroutine& rhs) noexcept { 
        HCE_TRACE_METHOD_ENTER("swap", rhs);
        std::coroutine_handle<> h = handle_;
        handle_ = rhs.handle_;
        rhs.handle_ = h;
    }

    /// return true if the coroutine is done, else false
    inline bool done() const { 
        bool d = handle_.done();
        HCE_MIN_METHOD_BODY("done", std::boolalpha, d);
        return d; 
    }

    /// return the address of the underlying handle
    inline void* address() const { 
        HCE_MIN_METHOD_ENTER("address");
        return handle_.address(); 
    }
   
    /// return true if called inside a running coroutine, else false
    static inline bool in() { 
        bool ret = detail::coroutine::tl_this_coroutine(); 
        HCE_TRACE_FUNCTION_BODY("hce::coroutine::in():",ret);
        return ret;
    }

    /// return the coroutine running on this thread
    static inline coroutine& local() { 
        HCE_TRACE_FUNCTION_ENTER("hce::coroutine::local()");
        return *(detail::coroutine::tl_this_coroutine()); 
    }

    /// resume the coroutine 
    inline void resume() {
        HCE_MED_METHOD_ENTER("resume");
        auto& tl_co = detail::coroutine::tl_this_coroutine();

        // store parent coroutine pointer
        auto parent_co = tl_co;

        // set current coroutine ptr 
        tl_co = this; 
        
        // continue coroutine execution
        handle_.resume();

        /*
         Optimize for handle stealing awaitable blocking operations by not 
         expecting this handle to remain valid.
         */
        if(handle_) [[unlikely]] {
            // acquire the exception pointer
            auto eptr = 
                std::coroutine_handle<promise_type>::from_address(address())
                    .promise()
                    .eptr;

            // rethrow any caught exceptions from the coroutine
            if(eptr) [[unlikely]] { std::rethrow_exception(eptr); }
        }

        // restore the parent pointer
        tl_co = parent_co;
    }

protected:
    inline void destroy_() {
        HCE_MED_METHOD_BODY("destroy", handle_);
        handle_.destroy(); // destruct and deallocate memory 
    }

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
        virtual ~promise_type() { cleanup(); }

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
        virtual ~promise_type() { cleanup(); }

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
    static inline std::string info_name() { 
        return "hce::detail::coroutine::this_thread"; 
    }

    inline std::string name() const { return this_thread::info_name(); }

    // get the this_thread object associated with the calling thread
    static this_thread* get();

    // can only block the calling thread
    template <typename LOCK>
    static inline void block(LOCK& lk) {
        HCE_TRACE_FUNCTION_ENTER("hce::detail::coroutine::this_thread::block",hce::type::name<LOCK>());
        auto tt = this_thread::get();
        HCE_TRACE_FUNCTION_BODY("hce::detail::coroutine::this_thread::block",tt);
        tt->block_(lk);
        HCE_TRACE_FUNCTION_BODY("hce::detail::coroutine::this_thread::block",tt,", exit");
    }
    
    // unblock an arbitrary this_thread without lock synchronization
    inline void unblock() {
        HCE_TRACE_METHOD_ENTER("unblock");
        ready = true;
        cv.notify_one();
    }

    // unblock an arbitrary this_thread with lock synchronization
    template <typename LOCK>
    inline void unblock(LOCK& lk) {
        HCE_TRACE_METHOD_ENTER("unblock",hce::type::name<LOCK>());
        ready = true;
        lk.unlock();
        cv.notify_one();
    }

private:
    this_thread() { }

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
    yield() { HCE_LOW_CONSTRUCTOR(); }

    virtual ~yield() {
        HCE_LOW_DESTRUCTOR();

        if(hce::coroutine::in() && !awaited_){ 
            std::stringstream ss;
            ss << hce::coroutine::local()
               << "did not call co_await on "
               << *this;
            HCE_FATAL_LOG("%s",ss.str().c_str());
            std::terminate();
        }
    }

    static inline std::string info_name() { 
        return "hce::detail::coroutine::yield"; 
    }

    inline std::string name() const { return yield::info_name(); }

    inline bool await_ready() {
        HCE_LOW_METHOD_ENTER("await_ready");
        awaited_ = true;
        return false; 
    }

    // don't need to replace the handle, it's not moving
    inline void await_suspend(std::coroutine_handle<> h) { 
        HCE_LOW_METHOD_ENTER("await_suspend");
    }

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
        HCE_LOW_METHOD_ENTER("await_resume");
        return std::move(t); 
    }

    inline operator T() {
        if(coroutine::in()) [[unlikely]] { 
            std::stringstream ss;
            ss << hce::coroutine::local()
               << "did not call co_await on "
               << *this;
            HCE_FATAL_LOG("%s",ss.str().c_str());
            std::terminate();
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
    inline void await_resume() { HCE_LOW_METHOD_ENTER("await_resume"); }
};

/**
 @brief complex awaitables inherit shared functionality defined here

 `hce::awaitable` objects cannot be used by themselves, they must be inheritted 
 by another object (IE, `hce::awt<T>`) which implements the `await_resume()` 
 method.

 `hce::awaitable` objects are transient, they are not copiable and are intended 
 to not stay in existence long. Their intended usage is to either `co_await` the 
 result (if in a coroutine), immediately convert to output `T` (or let the 
 awaitable go out of scope, blocking the thread or the operation completes).

 This object manages an implementation of a pure virtual interface. It does 
 this as a type erasure strategy that allows for maintenance of a single unique 
 pointer type. It can be a bit awkward to write new implementations, but the 
 higher level objects and utilities in this library should accomodate most user 
 needs without requiring the user to implement their own.
 */
struct awaitable : public printable {
    struct await {
        /// determines how locking is accomplished by an awaiter of an awaitable
        enum policy {
            adopt, //< assume the lock is already locked
            defer //< assume the lock is unlocked but lock it when necessary
        };
    };

    struct resume {
        /// determines how locking is accomplished by a caller of awaitable::resume()
        enum policy {
            adopt, //< assume lock is held during resume(), unlocking when done
            lock, //< lock during resume(), unlocking when done
            no_lock //< neither lock nor unlock during resume()
        };
    };

    /**
     @brief pure virtual interface for an awaitable's implementation 

     Implements methods required by an awaitable to function in a simultaneously 
     coroutine-safe and thread-safe way. 

     This object is quite complex. The easiest way to understand it is to 
     realize that various blocking operations need to have an object which 
     implement this interface, because this interface is responsible for 
     implementating generic and safe coroutine blocking operations.

     The pure virtual functions are used within *other* functions which can be 
     considered this object's "true" API.

     IE, the following are called indirectly by the compiler when the `co_await` 
     keyword is used:
     - bool await_ready() 
     - void await_suspend(std::coroutine_handle<> h)

     The following are called by completed operations to notify the awaitable 
     it can unblock:
     - void resume()

     Typically, an implementation of interface doesn't need to directly 
     implement every pure virtual function, it can inherit a sequence of partial 
     sub-implementations. For example, implementations of `lock()`, `unlock()`
     and the locking policies are provided by partial implementation 
     `awaitable::lockable<INTERFACE,LOCK>`, which can be inheritted by other 
     objects.
     */
    struct interface : public printable {
        interface() { HCE_LOW_CONSTRUCTOR(); }
        interface(const interface& rhs) = delete;
        interface(interface&& rhs) = delete;

        virtual ~interface() { 
            HCE_TRACE_DESTRUCTOR();

            if(this->handle_) [[unlikely]] {
                // place handle in coroutine for printing purposes
                coroutine co(std::move(this->handle_));
                std::stringstream ss;
                ss << *this
                   << " was not resumed before being destroyed; it held " 
                   << co;
                HCE_FATAL_METHOD_BODY("~interface",ss.str().c_str());

                // Can't recover anyway, about to terminate. Leaving the handle 
                // in this destructor and cleaning up would cause a circular 
                // destructor call (IE, an object inside the coroutine would 
                // destruct the coroutine containing it), causing a very 
                // confusing error.
                co.release();
                std::terminate();
            }
        }

        /**
         @brief implement custom new to make use of thread local allocation caching

         This has the same constructor argument limitations as 
         `hce::coroutine::promise_type`
         */
        inline void* operator new(std::size_t n) noexcept {
            return hce::memory::allocate(n);
        }

        inline void operator delete(void* ptr) noexcept {
            hce::memory::deallocate(ptr);
        }
        
        interface& operator=(const interface& rhs) = delete;
        interface& operator=(interface&& rhs) = delete;

        static inline std::string info_name() { 
            return "hce::awaitable::interface"; 
        }

        inline std::string name() const { return interface::info_name(); }

        virtual inline bool awaited() final { return this->awaited_; }

        virtual inline bool await_ready() final {
            HCE_LOW_METHOD_ENTER("await_ready");

            // acquire the lock if implementation was not constructed with 
            // ownership
            if(this->await_policy() == await::policy::defer) { this->lock(); }

            // set awaited flag
            this->awaited_ = true;

            // call the ready code
            if(this->on_ready()) [[unlikely]] {
                HCE_TRACE_METHOD_BODY("await_ready","ready immediately");
                this->unlock();
                return true;
            } else [[likely]] {
                HCE_TRACE_METHOD_BODY("await_ready","about to suspend");
                return false;
            }
        }

        /// called by awaitable's await_suspend()
        virtual inline void await_suspend(std::coroutine_handle<> h) final {
            HCE_LOW_METHOD_ENTER("await_suspend");

            // still locked from await_ready()
            this->on_suspend();

            // If this handle is valid, we are in a coroutine. Optimize for 
            // coroutine suspends over system threads
            if(h) [[likely]] {
                HCE_TRACE_METHOD_BODY("await_suspend",h);

                // assign the handle to our member
                this->handle_ = h; 

                // the current coroutine no longer manages the handle
                coroutine::local().release(); 

                // Compiler now returns to the caller of coroutine::resume() 
                // when this function returns
            } else [[unlikely]] {
                // Behavior of system thread in this function is VERY different 
                // than in coroutines. We block here on a condition_variable 
                // until resume() is called, where-as in a coroutine it takes
                // control of the coroutine handle and suspends.

                // block the calling thread using traditional mechanisms
                this->tt_ = detail::coroutine::this_thread::get(); 

                HCE_TRACE_METHOD_BODY("await_suspend","tt_:",(void*)(this->tt_));

                // allow condition_variable::wait() to unlock `this`
                detail::coroutine::this_thread::block(*this);

                // we are now re-locked and resumed
            }

            // in both cases we need to exit this function unlocked
            this->unlock();
        }

        /**
         @brief unblock and resume a suspended operation 

         This should be called by a different coroutine or thread.

         Calling this method will allow the unblock the thread or suspended 
         coroutine (`co_await` will return to its caller). 

         @param m arbitary memory passed to on_resume()
         */
        virtual inline void resume(void* m) final {
            HCE_LOW_METHOD_ENTER("resume");

            auto rp = this->resume_policy();

            // acquire the lock
            if(rp == resume::policy::lock){ this->lock(); }

            // call the custom resumption code
            this->on_resume(m); 

            // optimize for coroutines over threads
            if(this->handle_) [[likely]] { 
                HCE_TRACE_METHOD_BODY("resume","to_destination");
                // unblock the suspended coroutine and push the handle to its 
                // destination. Make sure that handle_ is unset before passing 
                // to destination. There are certain cases where the the 
                // coroutine can be rescheduled where this can cause an error 
                // otherwise in "no-lock" scenarios.
                auto h = this->handle_;
                this->handle_ = std::coroutine_handle<>();
                if(rp != resume::policy::no_lock) { this->unlock(); }
                this->to_destination(h);
            } else [[unlikely]] {
                if(this->tt_) [[unlikely]] { 
                    HCE_TRACE_METHOD_BODY("resume","unblock");
                    // unblock the suspended thread 
                    auto tt = this->tt_;
                    this->tt_ = nullptr;
                    if(rp == resume::policy::no_lock) { tt->unblock(); }
                    else { tt->unblock(*this); }
                } else [[likely]] {
                    HCE_TRACE_METHOD_BODY("resume","not blocked");
                    // this was called before blocking occurred
                    if(rp != resume::policy::no_lock) { this->unlock(); }
                }
            }
        }

        /**
         This value is necessary to introspect when determining how lock() needs
         to be called by the `co_await`er.

         @return the lock policy for the `co_await`er
         */
        virtual await::policy await_policy() const = 0;

        /**
         This value is necessary to introspect when determining how lock() needs
         to be called by the `resume()`er.

         @return the lock policy for the `resume()`er
         */
        virtual resume::policy resume_policy() const = 0;

        /// acquire the awaitable's lock
        virtual void lock() = 0;

        /// release the awaitable's lock
        virtual void unlock() = 0;

        /**
         This is called during resume() with the suspended coroutine handle, and
         is responsible for scheduling the handle for execution. IE, the handle 
         is ready to have its `resume()` method called.

         The lock is unlocked during this call.
         */
        virtual void to_destination(std::coroutine_handle<>) = 0;

        /**
         @brief returns whether the operation can resume immediately or not 

         This operation is where any code which needs to execute before suspend 
         should be called. The lock will be held while calling this method.
         */
        virtual bool on_ready() = 0;

        /**
         @brief operation which needs to execute immediately prior to suspension

         At this point the lock will be held and hce::coroutine::in() will 
         return `false` because the awaitable has taken responsibility for 
         the coroutine handle.
         */
        virtual void on_suspend() = 0;

        /**
         @brief called when resume()ing the suspended operation.  

         It is passed whatever arbitary memory is passed to resume(). The 
         implementation may use this memory to complete an operation in 
         whatever manner it sees fit. The lock will be held while calling this 
         method if `resume_policy() == resume::policy::lock`.

         @param m arbitrary memory
         */
        virtual void on_resume(void* m) = 0;

    private:
        bool awaited_ = false;
        detail::coroutine::this_thread* tt_ = nullptr;
        std::coroutine_handle<> handle_;
    };

    /**
     @brief partial implementation of awaitable::interface for a templated Lock 

     For 'lockfree' semantics, template the object to `hce::lockfree` and pass 
     an `hce::lockfree` reference to the constructor.

     Enables std::unique_lock<Lock>-like semantics.
     */
    template <typename Lock, typename INTERFACE>
    struct lockable : public INTERFACE {
        template <typename... As>
        lockable(Lock& lk, 
                 await::policy ap, 
                 resume::policy rp, 
                 As&&... as) :
            // construct INTERFACE with the remaining arguments
            INTERFACE(std::forward<As>(as)...),
            lk_(&lk),
            await_policy_(ap),
            resume_policy_(rp),
            locked_(ap == await::policy::adopt)
        { }

        /// ensure lock is unlocked when we go out of scope
        virtual ~lockable() { if(locked_){ unlock(); } }

        inline await::policy await_policy() const { 
            HCE_TRACE_METHOD_BODY("await_policy",await_policy_);
            return await_policy_; 
        }

        inline resume::policy resume_policy() const { 
            HCE_TRACE_METHOD_BODY("resume_policy",resume_policy_);
            return resume_policy_; 
        }

        inline void lock() final { 
            HCE_LOW_METHOD_ENTER("lock");
            lk_->lock(); 
            locked_ = true;
        }

        inline void unlock() final { 
            HCE_LOW_METHOD_ENTER("unlock");
            locked_ = false;
            lk_->unlock(); 
        }

    private:
        Lock* lk_;
        const await::policy await_policy_;
        const resume::policy resume_policy_;
        bool locked_; // track locked state
    };

    awaitable() : impl_(nullptr) {
        HCE_TRACE_CONSTRUCTOR();
    }

    awaitable(const awaitable&) = delete;
    awaitable(awaitable&& rhs) = default;
    
    ~awaitable() { 
        HCE_TRACE_DESTRUCTOR();
        wait(); 
    }

    inline awaitable& operator=(const awaitable&) = delete;
    inline awaitable& operator=(awaitable&& rhs) = default;
    
    static inline std::string info_name() { return "hce::awaitable"; }
    inline std::string name() const { return awaitable::info_name(); }

    inline std::string content() const {
        return impl_ ? impl_->to_string() : std::string();
    }

    /**
     Can't use conversion `operator bool()` because descendent awaitables are 
     implicitly convertable to a type `T`, and some fundamental types can 
     conflict with a boolean conversion. Better to have an explicit function to 
     check if the awaitable contains an implementation.

     @return true if the awaitable manages an implementation, else false
     */
    inline bool valid() const { return (bool)impl_; }

    /// return the underlying implementation
    interface& implementation() { return *impl_; }

    /// destroy the implementation 
    inline void reset() { impl_.reset(); }

    /// destroy the implementation and assign a new one
    inline void reset(interface* i) { impl_.reset(i); }

    /// release control of the underlying implementation pointer
    inline interface* release() { return impl_.release(); }

    /**
     Immediately called by `co_await` keyword. If it returns `true`, 
     `await_ready()` is immediately called.
     */
    inline bool await_ready() { return impl_->await_ready(); }

    /**
     When using the `co_await` keyword, if `await_ready() == false`, this 
     function is called by the compiler with a coroutine handler capable of 
     resuming where the coroutine was suspended. Store the coroutine handle so 
     we can resume later.

     If the argument handle does not represent a coroutine (`handle == false`), 
     then the operation is on a system thread and will block the calling thread 
     instead of simply returning.
     */
    inline void await_suspend(std::coroutine_handle<> h) { 
        impl_->await_suspend(h); 
    }

    /**
     @brief block until awaitable is complete and cleanup the implementation

     Should not be called by a coroutine, which should first use the 
     `co_await` keyword. If previously `co_await`ed this method will cleanup the 
     operation.

     This method automatically called by the destructor.
     */
    inline void wait() {
        HCE_MIN_METHOD_ENTER("wait");
        if(impl_ && !(impl_->awaited())) [[unlikely]] {
            if(coroutine::in()) [[unlikely]] { 
                // coroutine failed to `co_await` the awaitable
                std::stringstream ss;
                ss << hce::coroutine::local()
                   << "did not call co_await on "
                   << *this;
                HCE_FATAL_METHOD_BODY("wait",ss.str());
                std::terminate();
            } else if(!await_ready()) [[likely]] { 
                HCE_TRACE_METHOD_BODY("wait","thread");
                // if we're here, this awaitable is operating without the 
                // `co_await` keyword, and needs to operate as a regular system 
                // thread blocking call, not a coroutine suspend.
                await_suspend(std::coroutine_handle<>()); 
            } else {
                HCE_TRACE_METHOD_BODY("wait","thread done");
            }
        } else {
            HCE_TRACE_METHOD_BODY("wait","nothing to do");
        }
    }

protected:
    /*
     Construct an awaitable with some allocated awaitable::implementation 

     awaitables act like an `std::unique_ptr` when they are created with an 
     pointer to an implementation, they will delete their pointer when they go 
     out of scope.
     */
    template <typename IMPLEMENTATION>
    awaitable(IMPLEMENTATION* i) : 
        impl_(dynamic_cast<awaitable::interface*>(i))
    { }

private:
    struct interface_deleter {
        inline void operator()(interface* ptr) const noexcept {
            ptr->~interface(); // call destructor
            ptr->operator delete(ptr);  // call custom delete 
        }
    };

    std::unique_ptr<interface, interface_deleter> impl_;
};

/**
 @brief typed awaitable used by this library 

 This object wraps a type erased implementation of `awt<T>::interface`.

 If used by non-coroutines then converting/casting this object to its templated 
 type `T` or the object being destroyed without `co_await`ing it will block the 
 thread until the operation completes. 

 Likewise, when a coroutine `co_await`s on this object the coroutine will 
 suspend until the operation completes. If a coroutine fails to `co_await` the 
 object and it is destroyed within a coroutine then an exception will be thrown.

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
    typedef T value_type;

    // the necessary additional operations required by an instance of awt<T>
    struct interface : public awaitable::interface {
        // Ensure destructor is virtual to properly destruct 
        virtual ~interface() { 
            HCE_LOW_DESTRUCTOR();
        }

        static inline std::string info_name() { 
            return hce::awt<T>::info_name() + "::interface"; 
        }

        inline std::string name() const { return interface::info_name(); }

        ///return the final result of the `awaitable<T>`
        virtual T get_result() = 0;
    };

    awt(){}
    awt(const awt<T>& rhs) = delete;
    awt(awt<T>&& rhs) = default;

    // must be able to cast the implementation to the required parent type
    template <typename IMPLEMENTATION>
    awt(IMPLEMENTATION* i) : awaitable(dynamic_cast<interface*>(i)) { }

    ~awt(){ }
    
    inline awt& operator=(const awt<T>& rhs) = delete;
    inline awt& operator=(awt<T>&& rhs) = default;

    static inline std::string info_name() { 
        return type::templatize<T>("hce::awt"); 
    }

    inline std::string name() const { return awt<T>::info_name(); }

    /// construct an awt<T> from an interface implementation
    template <typename IMPLEMENTATION, typename... As>
    static inline awt<T> make(As&&... as) { 
        return awt<T>(new IMPLEMENTATION(std::forward<As>(as)...)); 
    }

    /**
     Return the final result of `awt<T>`. This operation is called by the 
     compiler as the result of the `co_await` keyword
     */
    inline T await_resume(){ 
        HCE_MIN_METHOD_ENTER("await_resume");
        return dynamic_cast<interface&>(this->implementation()).get_result();
    }

    /**
     Inline conversion for use in standard threads where `co_await` isn't called.
     */
    inline operator T() {
        HCE_MIN_METHOD_ENTER(std::string("operator ") + type::name<T>());
        this->wait(); 
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

 This object has the unique ability to safely wrap implementations of other 
 allocated `awt<T>::interface` implementations, with the caveat that the return 
 value `T` will not be extracted.
 */
template <>
struct awt<void> : public awaitable {
    typedef void value_type;

    struct interface : public awaitable::interface {
        virtual ~interface() { 
            HCE_LOW_DESTRUCTOR();
        }

        static inline std::string info_name() { 
            return hce::awt<void>::info_name() + "::interface"; 
        }

        inline std::string name() const { return interface::info_name(); }
    };

    awt(){}
    awt(const awt<void>& rhs) = delete;
    awt(awt<void>&& rhs) = default;

    // don't need to ensure return type void, because this type can handle 
    // *not* returning type T and is used to type erase other awaitables
    template <typename IMPLEMENTATION>
    awt(IMPLEMENTATION* i) : awaitable(i) { }

    ~awt() { }
    
    inline awt<void>& operator=(const awt<void>& rhs) = delete;
    inline awt<void>& operator=(awt<void>&& rhs) = default;
    
    static inline std::string info_name() { 
        return type::templatize<void>("hce::awt"); 
    }

    inline std::string name() const { return awt<void>::info_name(); }

    /// construct an awt<T> from an interface implementation
    template <typename IMPLEMENTATION, typename... As>
    static inline awt<void> make(As&&... as) { 
        return awt<void>(new IMPLEMENTATION(std::forward<As>(as)...)); 
    }

    inline void await_resume(){ 
        HCE_MIN_METHOD_ENTER("await_resume");
    }
};

/* 
 For some reason templates sometimes need derived templates to satisfy the 
 compiler. `typename` specifiers are confusing to everyone!
 */
template <typename T>
using awt_interface = typename hce::awt<T>::interface;

}

#endif
