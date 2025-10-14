//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#ifndef HERMES_COROUTINE_ENGINE_AWAITABLE
#define HERMES_COROUTINE_ENGINE_AWAITABLE

// c++
#include <string>
#include <coroutine>
#include <memory>
#include <sstream>
#include <string>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <cstddef>

// local
#include "utility.hpp"
#include "logging.hpp"
#include "atomic.hpp"
#include "memory.hpp"
#include "alloc.hpp"
#include "coroutine.hpp"

namespace hce {
namespace detail {
namespace awaitable {

struct yield : public hce::printable {
    yield();
    virtual ~yield();
    static std::string info_name();
    std::string name() const;
    bool await_ready();
    void await_suspend(std::coroutine_handle<> h);

private:
    bool awaited_ = false;
};

/// thread_local block/unblock functionality
struct this_thread : public printable {
    static std::string info_name();
    std::string name() const;

    // get the this_thread object associated with the calling thread
    static this_thread* get();

    // can only block the calling thread
    template <typename LOCK>
    static inline void block(LOCK& lk) {
        HCE_TRACE_FUNCTION_ENTER("hce::detail::awaitable::this_thread::block",hce::type::name<LOCK>());
        auto tt = this_thread::get();
        HCE_TRACE_FUNCTION_BODY("hce::detail::awaitable::this_thread::block",tt);
        tt->block_(lk);
        HCE_TRACE_FUNCTION_BODY("hce::detail::awaitable::this_thread::block",tt,", exit");
    }
    
    // unblock an arbitrary this_thread without lock synchronization
    inline void unblock() {
        HCE_TRACE_METHOD_ENTER("unblock");
        ready_ = true;
        cv_.notify_one();
    }

    // unblock an arbitrary this_thread with lock synchronization
    template <typename LOCK>
    inline void unblock(LOCK& lk) {
        HCE_TRACE_METHOD_ENTER("unblock",hce::type::name<LOCK>());
        ready_ = true;
        lk.unlock();
        cv_.notify_one();
    }

private:
    this_thread() { }

    template <typename LOCK>
    inline void block_(LOCK& lk) {
        while(!ready_) { 
            cv_.wait(lk); 
        }

        ready_ = false; // reset flag
    }

    bool ready_ = false;
    std::condition_variable_any cv_;
};

}
}

/**
 @brief `co_await` to suspend execution and allow other coroutines to run. 

 To temporarily suspend to caller of coroutine `resume()`:
 ```
 int i = co_await hce::yield<int>(3); // i == 3 when coroutine resumes
 ```

 This is a complete awaitable for usage with the `co_await` keyword.

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
 */
template <typename T>
struct yield : public detail::awaitable::yield {
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
struct yield<void> : public detail::awaitable::yield {
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

 `hce::awaitable` objects are transient, they are not copiable (though they can 
 be moved) and are intended to stay in existence briefly. Their intended usage 
 is to either `co_await` the result (if in a coroutine), immediately convert to 
 output `T` (or let the awaitable go out of scope, which blocks the thread or 
 the operation completes).

 This object manages an implementation of a pure virtual interface. It does 
 this as a type erasure strategy that allows for maintenance of a single pointer 
 type. It can be a bit awkward to write new implementations, but the higher 
 level objects and utilities in this library which implement awaitables should 
 accomodate most user needs without requiring the user to implement their own.
 */
struct awaitable : public printable {
    /// awaitable::interface::state() bit masks 
    struct masks {
        /// holds the ready-only await locking policy
        static constexpr uint8_t await_policy = 1;

        /// holds the ready-only resumed locking policy
        static constexpr uint8_t resumed_policy = 1 << 1;

        /// holds the ready-only resume locking policy
        static constexpr uint8_t notify_policy = 1 << 2;

        /**
         Indicates if the awaitable interface is locked. Reading this bit is 
         only useful internally for determing cleanup behavior when an awaitable 
         is immediately ready or after it has been notified. IE, it is used to 
         enforce the resumed policy.
         */
        static constexpr uint8_t locked = 1 << 3;

        /**
         Indicates if the awaitable has ever been awaited. This bit is only 
         ever read or written by the awaiter.
         */
        static constexpr uint8_t awaited = 1 << 4;

        /// indicates if the awaiter is suspended
        static constexpr uint8_t suspended = 1 << 5;

        /**
         Bit indicates if the suspended awaiter is a coroutine 

         This bit only has meaning if the suspended bit is 1. This bit set to 1 
         means its a suspended coroutine, 0 means it is a non-coroutine 
         (thread).
         */
        static constexpr uint8_t is_coroutine = 1 << 6;

        // bit 7 is free 
    };
    
    struct await {
        /** 
         Determines how locking is accomplished by caller of `co_await` on an 
         awaitable.
         */
        enum policy {
            defer_lock = 0x0, //< assume the lock is unlocked but lock it when necessary
            adopt_lock = masks::await_policy //< assume the lock is already locked
        };
    };

    struct resumed {
        /**
         Determines the state of the lock when awaiter of an awaitable resumes 
         control when returning from `co_await`.
         */
        enum policy {
            unlocked = 0x0, //< lock is not held after awaiter resumes
            locked = masks::resumed_policy //< lock is held when awaiter resumes
        };
    };

    struct notify {
        /**
         Determines how locking is accomplished by a caller of 
         hce::awaitable::interface::notify().

         This policy's purpose is to handle the cases where notify()'s 
         calling code either:
         1) Does no explicit synchronization, in which case `lock_responsible` 
         should be used so the notify() call is internally synchronized.
         2) The calling code is managing ALL locking and unlocking 
         synchronization, in which case `lock_exempt` should be used. 

         The latter case is necessary in complicated usecases where awaitables 
         must closely interact with a parent object, as is the case in channels.
         */
        enum policy {
            lock_exempt = 0x0, //< do not manage lock during notify() 
            lock_responsible = masks::notify_policy //< lock and unlock during notify()
        };
    };

    /**
     @brief pure virtual single-use interface for an awaitable's implementation 

     Implements logic required by an awaitable to be used in a simultaneously 
     coroutine-safe and thread-safe way. 

     The pure virtual functions are called by a descendant implementation of 
     `hce::awaitable` by the compiler when the `co_await` keyword is used on it. 
     That is, these functions internally call virtual interface implementations:
     - bool await_ready() 
     - void await_suspend(std::coroutine_handle<> h)
     - void await_resume()

     The following are called by completed operations to notify the awaitable 
     it is done and can unblock:
     - void notify(void*)

     `hce::awaitable::interface` is not inheritted directly. Instead, 
     implementations should inherit `hce::awt<T>::interface`, which implements 
     the final ability to get a returned value `T` from an operation 
     (`hce::awt<void>::interface` is used for operations returning nothing).

     Some of the virtual API can be implemented by other objects which inherit
     `hce::awt<T>::interface`.

     `lock_impl()`/`unlock_impl()` can be implemented by:
     - `hce::awaitable::lockable<Lock,INTERFACE>`
     - `hce::awaitable::spinlock_lockable<INTERFACE>`
     - `hce::awaitable::lockfree_lockable<INTERFACE>`

     `to_destination()` can be implemented by:
     - `hce::scheduler::reschedule<INTERFACE>`

     `info_name()`/`name()`/`deleter()` have default implementations that can be 
     overridden.

     In the various partial implementations `INTERFACE` is the type that the
     template must inherit. All remaining optional arguments `as...` will be 
     passed to type `INTERFACE`'s contructor. 

     Putting it all together, here's an example declaration of a of a
     complete `hce::awt<T>::interface` implementation:
     ```
     struct my_awaitable : public 
         hce::scheduler::reschedule<
            hce::awaitable::spinlock_lockable<
                hce::awt<bool>::interface>>
     {
         awaitable();
         virtual ~awaitable();
         static std::string info_name();
         std::string name() const;
         bool on_ready();
         void on_notify(void* m);
         bool get_result();

     private:
         bool result_;
     };
     ```

     Definitions might be:
     ```
     my_awaitable::my_awaitable() : 
         hce::scheduler::reschedule<
             hce::awaitable::spinlock_lockable<
                 hce::awt<bool>::interface>>(
                     // reschedule and spinlock_lockable pass these arguments 
                     // down to the `hce::awt<bool>::interface` constructor.
                     hce::awaitable::await::policy::defer_lock,
                     hce::awaitable::resumed::policy::unlocked,
                     hce::awaitable::notify::policy::lock_responsible),
         result_(false)
     { }

     my_awaitable::~my_awaitable(){ }
     std::string my_awaitable::info_name() { return "my_awaitable"; }

     std::string my_awaitable::name() const { 
         return my_awaitable::info_name(); 
     }

     bool my_awaitable::on_ready() {
         // this would be were the implementation would be responsible for 
         // registering its pointer to some other object or location to be 
         // resumed sometime in the future
         return false; // never ready immediately
     }

     void my_awaitable::on_notify(void* m) { 
         result_ = (bool)m; // interpret the void* in some implicitly agreed way
     }

     bool my_awaitable::get_result() { return result_; }
     ```
     */
    struct interface : public printable {
        /// function type used for deleting interface instance
        typedef void (*deleter_t)(interface*);

        /**
         Initialize the interface with the given policies. Policy bits are read 
         only once constructed.
         */
        interface(uint8_t policy_bits);
        interface(const interface& rhs) = delete;
        interface(interface&& rhs) = delete;

        virtual ~interface();
        
        interface& operator=(const interface& rhs) = delete;
        interface& operator=(interface&& rhs) = delete;

        /**
         Map calls to allocation portion of `new` keyword to framework 
         allocation function.
         */
        inline void* operator new(std::size_t n) noexcept {
            return hce::memory::allocate(n);
        }

        /**
         Map calls to deallocation portion of `delete` keyword to framework 
         deallocation function.
         */
        inline void operator delete(void* ptr) noexcept {
            hce::memory::deallocate(ptr);
        }

        //----------------------------------------------------------------------
        /** co_await methods
        
         These methods are intended to called by the `co_await` keyword in a
         running coroutine OR by the `wait()` method in a standard thread by the
         owning `hce::awt<T>` instance.

         In standard threads (non-coroutines) the `wait()` method is implicitly 
         called when either the `hce::awt<T>` object goes out of scope or is 
         implictly converted to its return type `T` in an assignment:
         ```
         // given my_function() returning hce::awt<T>()
         T my_t = my_function(); // implicit conversion calls `wait()`
         ```

         WARNING: If `await_ready()` has not been called when the destructor 
         runs, and the code is executing inside a coroutine, this will cause a
         fatal error causing the program to terminate. That is, 
         `hce::awaitable::interface` implementations MUST be `co_await`ed when 
         used in a coroutine:
         ```
         // given my_function() returning hce::awt<T>()
         T my_t = co_await my_function(); // use co_await in a coroutine to get T
         ```
         */
         
        /// Called by `hce::awaitable` 
        bool await_ready();

        /// Called by `hce::awaitable`
        void await_suspend(std::coroutine_handle<> h);

        /// Called by `hce::awt<T>`, which inherits `hce::awaitable`
        void await_resume();

        //----------------------------------------------------------------------
        // public API

        /**
         @brief retrieve the state of the awaitable 

         This value can be parsed using `hce::awaitable` mask values. This 
         value is primarily exposed in case debugging is necessary.
         */
        uint8_t state() const;

        /*
         This value is necessary to introspect when determining how lock() needs
         to be called by the `co_await`er.
         */
        hce::awaitable::await::policy await_policy() const;

        /*
         This value is necessary to introspect when determining how lock() needs
         to be called by awoken coroutine/thread from await_suspend.
         */
        hce::awaitable::resumed::policy resumed_policy() const;

        /*
         This value is necessary to introspect when determining if to lock 
         during a call to `notify()`.
         */
        hce::awaitable::notify::policy notify_policy() const;

        /**
         @brief notify an operation is ready to resume from suspension

         This should be called by a different coroutine or thread.

         Calling this method will unblock a suspended thread or coroutine 
         (`wait()` and `co_await` will return to its caller). 

         The argument void* is passed to the implementation's on_notify(), which 
         is responsible to interpret its meaning, allowing arbitary 
         communication from the caller of notify() the suspended operation.

         The behavior of locking when calling this method is determined by the 
         configured `hce::awaitable::notify::policy`

         WARNING: Consideration must be given to how `on_ready()` is written if 
         it is possible for this to be called before `on_ready()` is called. In 
         such a case, it is typically necessary for `on_ready()` to return 
         `true`.

         WARNING: It is an ERROR to call this method twice. Once notify()ed, any 
         pointer to the interface should be treated as volatile memory.

         @param m arbitary memory passed to notify()
         */
        void notify(void* m);

        /**
         @brief acquire the awaitable's lock  

         Should only be considered for use when:
         1) `notify()` is called while `notify::policy::lock_exempt` is set
         2) by callers of `co_await`/`wait()` when `await::policy::adopt_lock` is set.
         */
        void lock();

        /**
         @brief release the awaitable's lock  

         Should only be considered for use when:
         1) `notify()` is called while `notify::policy::lock_exempt` is set
         2) by callers of `co_await`/`wait()` when `resumed::policy::locked` is set.
         */
        void unlock();

        //----------------------------------------------------------------------
        /** Virtual API
         
         The virtual API describes the interface methods that descendant code 
         most consider and implement.
         */

        // overrideable default info_name() implementing `hce::printable`
        static std::string info_name();

        // overrideable default name() implementing `hce::printable`
        std::string name() const;

        /**
         The default implementation executes `delete` keyword on the instance.

         Can be customized to pass the interface pointer to some other code in 
         scenarios where specially allocated memory should be handled 
         differently.

         @return the function which deletes this interface instance 
         */
        virtual deleter_t deleter();

        /// underlying implementation to acquire a lock
        virtual void lock_impl() = 0;

        /// underlying implementation to release a lock
        virtual void unlock_impl() = 0;

        /**
         @brief code called during `await_ready()` 

         The lock will be held while calling this method.

         This is where code needed to determine if the implementation needs to 
         suspend should be placed. If the operation returns `false`, indicating 
         it is is not ready, this method is responsible for passing the 
         interface implementation's pointer to some location which can 
         `notify()` when to resume.

         @return true if the operation is ready immediately, false if needs to suspend
         */
        virtual bool on_ready() = 0;

        /**
         @brief operation which needs to execute immediately prior to suspension

         The lock will be held while calling this method and 
         `hce::coroutine::in()` will always return `false` because the awaitable 
         has taken responsibility for any running coroutine handle.

         This operation is where any code which needs to execute just before 
         suspending should be placed. It is typically used to cache information 
         for when and how to reschedule when resumed.
         */
        virtual void on_suspend() = 0;

        /**
         @brief called when notify()ing an operation.

         This provides a place for any necessary code to run just prior to the 
         operation being resumed.

         The lock will be held while calling this method if 
         `notify::policy::lock_responsible` is set. Otherwise it is the calling 
         code's responsibility to acquire the lock.

         It is passed whatever arbitary memory is passed to notify(). The 
         implementation may use this memory to complete an operation in 
         whatever manner it sees fit. 

         @param m arbitrary memory
         */
        virtual void on_notify(void* m) = 0;

        /**
         Underlying implementation to pass a coroutine to a destination which 
         will resume it. 

         This is *only* called when resuming a suspended coroutine.

         Specifically it is called during notify() with the suspended coroutine 
         handle, and is responsible for scheduling the handle for execution. IE, 
         the handle is ready to have its notify() method called.

         This is typically implemented by a rescheduling object.

         WARNING: The lock is unlocked during this call.
         */
        virtual void to_destination(std::coroutine_handle<>) = 0;

        /**
         IMPORTANT: All `hce::awt<T>` instances which are not `hce::awt<void>` 
         must implement:
         ```
         virtual T get_result() = 0;
         ```
         */

    private:
        // all union types are POD, don't need destructors
        union data {
            // Constructors to initialize specific members
            data() {} // Default constructor (optional, but harmless)
            data(std::coroutine_handle<> h) : handle(h) {}
            data(detail::awaitable::this_thread* tt) : this_thread(tt) {}

            // used if is_coroutine_() == true
            std::coroutine_handle<> handle;

            // used if is_coroutine_() == false
            detail::awaitable::this_thread* this_thread;
        };

        // used to properly size data_ bytes
        static constexpr uint8_t aligned_data_size = 
            ((sizeof(data)) + alignof(data) - 1) & ~(alignof(data) - 1);

        // the default function pointer to delete interface implementations
        static void default_deleter_(interface*);

        // state mutation
        void set_awaited_();
        void set_locked_();
        void set_suspended_();
        void set_is_coroutine_();
        void unset_locked_();
        void unset_suspended_();
        void unset_is_coroutine_();

        // acquire the data union cast from data_
        data& get_data_(); 

        /*
         `hce::awaitable::interface`s instances are created frequently, and 
         should use as little memory by default as possible, hence the state 
         masking and union. Each bit in `state_` represents a specific 
         configuration, as defined by the `hce::awaitable::masks`.
         */
        uint8_t state_;

        // data is uninitialized bytes until `is_suspended_() == true` (we need 
        // to construct late with placement new)
        alignas(alignof(data)) std::byte data_[aligned_data_size];
    };
   
    /**
     @brief partial implementation of awaitable::interface for a templated Lock 

     The lock is accessed via pointer, the underlying lock data is not a member 
     of this object.

     For 'lockfree' semantics, template the object to `hce::lockfree` and pass 
     an `hce::lockfree` reference to the constructor.

     Enables std::unique_lock<Lock>-like semantics.

     `INTERFACE` is type that this template must inherit. All remaining optional
     arguments `as...` will be passed to type `INTERFACE`'s contructor.
     */
    template <typename Lock, typename INTERFACE>
    struct lockable : public INTERFACE {
        template <typename... As>
        lockable(Lock& lk, As&&... as) :
            // construct INTERFACE with the remaining arguments
            INTERFACE(std::forward<As>(as)...),
            lk_(&lk)
        { }

        /// lock the lock
        inline void lock_impl() final { lk_->lock(); }

        /// unlock the lock
        inline void unlock_impl() final { lk_->unlock(); }

    private:
        Lock* lk_;
    };

    /**
     @brief spinlock implementation 

     A micro-optimization, eliding the need for storing a pointer and needing to 
     dereference the pointer when an implementation uses an isolated spinlock.

     `INTERFACE` is type that this template must inherit. All remaining optional
     arguments `as...` will be passed to type `INTERFACE`'s contructor.
     */
    template <typename INTERFACE>
    struct spinlock_lockable : public INTERFACE {
        template <typename... As>
        spinlock_lockable(As&&... as) :
            // construct INTERFACE with the remaining arguments
            INTERFACE(std::forward<As>(as)...)
        { }

        virtual ~spinlock_lockable() {}
        inline void lock_impl() final { lk_.lock(); }
        inline void unlock_impl() final { lk_.unlock(); }

    private:
        hce::spinlock lk_;
    };


    /**
     @brief lockfree implementation 

     A micro-optimization, eliding the need for storing a pointer and needing to 
     dereference the pointer when an implementation is lockfree.

     `INTERFACE` is type that this template must inherit. All remaining optional
     arguments `as...` will be passed to type `INTERFACE`'s contructor.
     */
    template <typename INTERFACE>
    struct lockfree_lockable : public INTERFACE {
        template <typename... As>
        lockfree_lockable(As&&... as) :
            // construct INTERFACE with the remaining arguments
            INTERFACE(std::forward<As>(as)...)
        { }
        
        virtual ~lockfree_lockable() {}
        inline void lock_impl() final { }
        inline void unlock_impl() final { }
    };

    awaitable() : impl_(nullptr,nullptr) {
        HCE_TRACE_CONSTRUCTOR();
    }

    awaitable(const awaitable&) = delete;
    awaitable(awaitable&& rhs) = default;
    
    virtual ~awaitable() { 
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
    inline void await_suspend(std::coroutine_handle<> h) { impl_->await_suspend(h); }

    /**
     @brief block until awaitable is complete and cleanup the implementation
     
     If the implementation is already awaited, wait() will return immediately.

     Should not be called by a coroutine, which should first use the 
     `co_await` keyword. If previously `co_await`ed this method will cleanup the 
     operation.

     This method automatically called by the destructor. 
     */
    void wait();

protected:
    /*
     Construct an awaitable with some allocated awaitable::implementation 

     awaitables act like an `std::unique_ptr` when they are created with an 
     pointer to an implementation, they will delete their pointer when they go 
     out of scope.
     */
    template <typename IMPLEMENTATION>
    awaitable(IMPLEMENTATION* i) : 
        impl_(dynamic_cast<awaitable::interface*>(i), i->deleter())
    { }

private:
    std::unique_ptr<interface, interface::deleter_t> impl_;
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
        template <typename... As>
        interface(As&&... as) :
            awaitable::interface(std::forward<As>(as)...)
        { }

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

    virtual ~awt(){ }
    
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
        auto& intf = dynamic_cast<interface&>(this->implementation());
        intf.await_resume();
        return intf.get_result();
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
        template <typename... As>
        interface(As&&... as) :
            awaitable::interface(std::forward<As>(as)...)
        { }

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

    virtual ~awt() { }
    
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
        dynamic_cast<interface&>(this->implementation()).await_resume();
    }
};

/* 
 For some reason templates sometimes need derived templates to satisfy the 
 compiler. `typename` specifiers are confusing to everyone!
 */
template <typename T>
using awt_interface = typename hce::awt<T>::interface;

/**
 @brief convert an awaitable that returns some type T to one that returns nothing 

 The awaitable still operates otherwise as normal but no type will be returned 
 from `co_await`ing it. 

 This mechanism is useful for blocking on operations with arbitrary return 
 types that are ignored.

 @return a rewrapped awaitable::interface in an awt<void>
 */
template <typename T>
inline awt<void> to_awt_void(awt<T> a) {
    return awt<void>(a.release());
}

/// Exception case where input is already an awt<void> is a simple passthrough 
template <>
inline awt<void> to_awt_void(awt<void> a) {
    return std::move(a);
}

}

#endif
