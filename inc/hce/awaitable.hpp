#pragma once
//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 

#include <coroutine>

#include "hce/printable.hpp"
#include "hce/type.hpp"
#include "hce/alloc.hpp"
#include "hce/fatal.hpp"

namespace hce {


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
        interface();
        interface(const interface& rhs) = delete;
        interface(interface&& rhs) = delete;

        virtual ~interface();

        /**
         @brief implement custom new to make use of thread local allocation caching

         This has the same constructor argument limitations as 
         `hce::coroutine::promise_type`
         */
        void* operator new(std::size_t n) noexcept;
        void operator delete(void* ptr) noexcept;
        
        interface& operator=(const interface& rhs) = delete;
        interface& operator=(interface&& rhs) = delete;

        static inline std::string info_name();
        std::string name() const;
        virtual bool awaited() final;

        virtual bool await_ready() final;

        /// called by awaitable's await_suspend()
        virtual void await_suspend(std::coroutine_handle<> h) final;

        /**
         @brief unblock and resume a suspended operation 

         This should be called by a different coroutine or thread.

         Calling this method will allow the unblock the thread or suspended 
         coroutine (`co_await` will return to its caller). 

         @param m arbitary memory passed to on_resume()
         */
        virtual void resume(void* m) final;

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
        virtual void to_destination(std::coroutine_handle<>);

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
        virtual void on_suspend();

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
        void* destination_ = nullptr;
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
            return await_policy_; 
        }

        inline resume::policy resume_policy() const { 
            return resume_policy_; 
        }

        inline void lock() final { 
            lk_->lock(); 
            locked_ = true;
        }

        inline void unlock() final { 
            locked_ = false;
            lk_->unlock(); 
        }

    private:
        Lock* lk_;
        const await::policy await_policy_;
        const resume::policy resume_policy_;
        bool locked_; // track locked state
    };

    awaitable();
    awaitable(const awaitable&) = delete;
    awaitable(awaitable&& rhs) = default;
    
    virtual ~awaitable();

    awaitable& operator=(const awaitable&) = delete;
    awaitable& operator=(awaitable&& rhs) = default;
    
    static std::string info_name();
    std::string name() const;

    std::string content() const;

    /**
     Can't use conversion `operator bool()` because descendent awaitables are 
     implicitly convertable to a type `T`, and some fundamental types can 
     conflict with a boolean conversion. Better to have an explicit function to 
     check if the awaitable contains an implementation.

     @return true if the awaitable manages an implementation, else false
     */
    bool valid() const;

    /// return the pointer to underlying implementation
    interface* get() const;

    /// return the underlying implementation
    interface& implementation();

    /// destroy the implementation 
    void reset();

    /// destroy the implementation and assign a new one
    void reset(interface* i);

    /// release control of the underlying implementation pointer
    interface* release();

    /**
     Immediately called by `co_await` keyword. If it returns `true`, 
     `await_ready()` is immediately called.
     */
    bool await_ready();

    /**
     When using the `co_await` keyword, if `await_ready() == false`, this 
     function is called by the compiler with a coroutine handler capable of 
     resuming where the coroutine was suspended. Store the coroutine handle so 
     we can resume later.

     If the argument handle does not represent a coroutine (`handle == false`), 
     then the operation is on a system thread and will block the calling thread 
     instead of simply returning.
     */
    void await_suspend(std::coroutine_handle<> h);

    /**
     @brief block until awaitable is complete and cleanup the implementation

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
        impl_(dynamic_cast<awaitable::interface*>(i))
    { }

private:
    struct interface_deleter {
        void operator()(interface* ptr) const noexcept;
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
        virtual ~interface() { }

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
        return dynamic_cast<interface&>(this->implementation()).get_result();
    }

    /**
     Inline conversion for use in standard threads where `co_await` isn't called.
     */
    inline operator T() {
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
        virtual ~interface() { }

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

    inline void await_resume(){ }
};

/* 
 For some reason templates sometimes need derived templates to satisfy the 
 compiler. `typename` specifiers are confusing to everyone!
 */
template <typename T>
using awt_interface = typename hce::awt<T>::interface; 

} // hce
