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
            // begin at bit 5
            adopt = 0x10, //< assume the lock is already locked
            defer //< assume the lock is unlocked but lock it when necessary
        };
    };

    struct resume {
        /// determines how locking is accomplished by a caller of awaitable::resume()
        enum policy {
            // begin at bit 6
            adopt = 0x20, //< assume lock is held during resume(), unlocking when done
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
     are provided by partial implementation 
     `awaitable::lockable<INTERFACE,LOCK>`, which can be inheritted by other 
     objects.
     */
    struct interface : public printable {
        interface(await::policy ap, resume::policy rp);
        interface(const interface& rhs) = delete;
        interface(interface&& rhs) = delete;

        virtual ~interface();

        inline void* operator new(std::size_t n) noexcept {
            return hce::memory::allocate(n);
        }

        inline void operator delete(void* ptr) noexcept {
            hce::memory::deallocate(ptr);
        }
        
        interface& operator=(const interface& rhs) = delete;
        interface& operator=(interface&& rhs) = delete;

        static std::string info_name();
        std::string name() const;
        virtual bool awaited() final;
        virtual bool await_ready() final;

        /// called by awaitable's await_suspend()
        virtual void await_suspend(std::coroutine_handle<> h) final;

        /**
         Do necessary cleanup operations while all memory is valid. Must be 
         called from descendant implementation's destructor.
         */
        void clean();

        /**
         @brief return the ready state

         The default implementation of `on_ready()` (called in `await_ready()`) 
         returns this value. If the user provides an alternative definition to 
         `on_ready()`, then the value of this flag is only relevant if the user 
         definition utilizes it.
         */
        bool ready() const;

        /**
         @brief set the ready state 
         @param b the new ready state 
         */
        void ready(bool b);

        /**
         This value is necessary to introspect when determining how lock() needs
         to be called by the `co_await`er.

         @return the lock policy for the `co_await`er
         */
        hce::awaitable::await::policy await_policy() const;

        /**
         This value is necessary to introspect when determining how lock() needs
         to be called by the `resume()`er.

         @return the lock policy for the `resume()`er
         */
        hce::awaitable::resume::policy resume_policy() const;

        /**
         @brief unblock and resume a suspended operation 

         This should be called by a different coroutine or thread.

         Calling this method will allow the unblock the thread or suspended 
         coroutine (`co_await` will return to its caller). 

         @param m arbitary memory passed to on_resume()
         */
        void resume(void* m);

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

         The default implementation of this function returns ready().
         */
        virtual bool on_ready();

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

        static constexpr uint8_t awaited_mask_ = 1u << 0;
        static constexpr uint8_t has_pointer_mask_ = 1u << 1;
        static constexpr uint8_t is_coroutine_mask_ = 1u << 2;
        static constexpr uint8_t locked_mask_ = 1u << 3;
        static constexpr uint8_t await_policy_mask_ = 1u << 4;
        static constexpr uint8_t resume_policy_mask_ = 0x60;
        static constexpr uint8_t ready_mask_ = 1u << 7;

        bool awaited_() const;
        void awaited_(bool b);
        bool has_pointer_() const;
        void has_pointer_(bool b);
        bool is_coroutine_() const;
        void is_coroutine_(bool b);
        bool locked_() const;
        void locked_(bool b);
        data& get_data_();
        void lock_();
        void unlock_();

        // `hce::awaitable::interface`s instances are created frequently, and 
        // should use as little memory by default as possible, hence the state 
        // masking and union. Each bit in `state` represents a specific 
        // configuration, as defined by the masking above.
        uint8_t state_ = 0;

        // data is uninitialized bytes until has_pointer_() == true
        std::byte data_[sizeof(data)];
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
        lockable(Lock& lk, As&&... as) :
            // construct INTERFACE with the remaining arguments
            INTERFACE(std::forward<As>(as)...),
            lk_(&lk)
        { }

        inline void lock() final { lk_->lock(); }
        inline void unlock() final { lk_->unlock(); }

    private:
        Lock* lk_;
    };

    /**
     @brief spinlock implementation 

     A micro-optimization, eliding the need for storing a pointer and needing to 
     dereference the pointer when an implementation uses an isolated spinlock.
     */
    template <typename INTERFACE>
    struct spinlock_lockable : public INTERFACE {
        template <typename... As>
        spinlock_lockable(As&&... as) :
            // construct INTERFACE with the remaining arguments
            INTERFACE(std::forward<As>(as)...)
        { }

        virtual ~spinlock_lockable() {}

        inline void lock() final { lk_.lock(); }
        inline void unlock() final { lk_.unlock(); }

    private:
        hce::spinlock lk_;
    };


    /**
     @brief lockfree implementation 

     A micro-optimization, eliding the need for storing a pointer and needing to 
     dereference the pointer when an implementation is lockfree.
     */
    template <typename INTERFACE>
    struct lockfree_lockable : public INTERFACE {
        template <typename... As>
        lockfree_lockable(As&&... as) :
            // construct INTERFACE with the remaining arguments
            INTERFACE(std::forward<As>(as)...)
        { }
        
        virtual ~lockfree_lockable() {}

        inline void lock() final { }
        inline void unlock() final { }
    };

    awaitable() : impl_(nullptr) {
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
