#pragma once
//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 

#include <thread>
#include <mutex>
#include <array>
#include <map>
#include <coroutine>
#include <memory>

// Platform-specific includes
#ifdef _WIN32
#include <windows.h>
#elif defined(_POSIX_VERSION)
#include <pthread.h>
#include <sched.h>
#endif

#include "base.hpp"
#include "cleanup.hpp"
#include "logging.hpp"
#include "atomic.hpp"
#include "service.hpp"

namespace hce {
namespace thread {

/// keys for various thread locals
enum key {
    loglevel,
    coroutine,
    coroutine_thread,
    scheduler,
    scheduler_local_queue,
    FINAL // this value is not used, must be last
};

// forward declare key to type mapping
template <key KEY>
struct key_map_t;

/**
 @brief a process-wide service for propogating initial thread_local pointer-references

 This service is a dependency of logging loglevels, and therefore cannot 
 implement `hce::printable`.
 */
class local : public hce::service<local> {
    // define the lookup table as an std::array of void* initialized to nullptr
    struct table_t : public hce::cleanup {
        table_t();
        virtual ~table_t();

        /// implement cleanup interface
        void* cleanup_alloc(size_t sz);

        /// implement cleanup interface
        void cleanup_dealloc(void* p);

        // the total count of ptr<T>s in existence for the thread associated 
        // with this table
        size_t use_count;

        // the pointers associated with a given hce::thread::key are stored in 
        // this lookup table
        std::array<void*, key::FINAL> lookup;
    };

public:
    /**
     @brief a thread local pointer-reference abstraction

     WARNING: This object MUST be destroyed by the thread which created it. It 
     is intended to be created as a `thread_local` (the compiler keyword) value,
     or as a member of a `thread_local` object.

     This object acts as an RAII manager (similar to an std::shared_ptr) for a
     `thread_local` pointer-reference. Pointer references are necessary for many 
     performance critical hce mechanisms which need to manipulate runtime 
     indirection. Once constructed this mechanism is just as fast as a normal 
     `thread_local` pointer reference.
     
     This abstraction is necessary to handle imported shared libraries which 
     can actually have duplicate `thread_local` instances constructed by its 
     private translation units. To address this problem `thread_local` pointer 
     values are initialized to pointers maintained by the `hce::thread::local` 
     global service accessed through instances of this object.

     Operations on the returned `T*&` pointer reference from `ref()` are 
     thread-safe because all accesses to the `hce::thread::local` service are 
     mutex locked and internally mapped to a context associated with only the 
     calling thread (`std::this_thread::get_id()` is used to disambiguate during 
     `ptr<T>` construction/destruction). Modification of the pointers stored in 
     `hce::thread::local` without mutex synchronization are thread-safe during 
     the `thread_local` `ptr<T>`'s lifetime because once the values are 
     initialized only the calling thread has access to it. 

     This mechanism only supports pointer references, the actual memory for 
     any object `T` is held elsewhere, typically on a thread stack or in some 
     `hce::service` implementation.

     Example usage:
     ```
     // thread_local pointer-reference accessor
     my_object*& thread_local_my_object() {
        // initialization of the `thread_local` only happens when 
        // thread_local_my_object() is first called by the process or library
        thread_local hce::thread::local::ptr<hce::thread::my_object_key> p;
        return p.ref();
     }
     ```

     KEY is the hce::thread::key associated with the pointer.
     */
    template <key KEY>
    struct ptr {
        // the actual pointer type managed by this object
        using T = typename key_map_t<KEY>::type;

        ptr() : 
            table_(hce::service<hce::thread::local>::get().acquire_()),
            ref_(*reinterpret_cast<T**>(&((table_->lookup)[KEY])))
        { }

        ptr(const ptr<KEY>&) = delete;
        ptr(ptr<KEY>&& rhs) = delete;

        ~ptr() {
            /* 
             It is not necessarily an error if the service isn't ready, we could 
             be on the main thread and the `hce::lifecycle` is out of scope when 
             the thread_local ptr<T> is finally destroyed. In that case all 
             that matters is that all operations which would utilize the 
             `thread_local` ptr<T> have ceased prior to the `hce::lifecycle` 
             being destroyed.
             */
            if(hce::service<hce::thread::local>::ready()) {
                hce::service<hce::thread::local>::get().release_(table_);
            }
        }

        ptr<KEY>& operator=(const ptr<KEY>&) = delete;
        ptr<KEY>& operator=(ptr<KEY>&& rhs) = delete;

        /*
         Install a cleanup handler into the thread local table that will be 
         called when the last `ptr` goes out of scope.
         */
        inline void install(cleanup::operation op, void* i) {
            table_->install(op,i);
        }

        /// get the configured pointer
        inline T*& ref() { return ref_; }

    private:
        table_t* table_;
        T*& ref_;
    };

    virtual ~local();

private:
    local();

    // acquire a the thread local table for a ptr
    table_t* acquire_();

    // inform the service a ptr is releasing
    void release_(table_t* table);

    hce::spinlock lk_;

    // std::map is used instead of std::unordered_map for insertion/erasure 
    // speed, as it is only accessed in those cases
    std::map<std::thread::id,std::unique_ptr<table_t>> thread_map_;

    friend hce::lifecycle;
};

/**
 @brief attempt to set a thread's priority in a system agnostic way 
 */
bool set_priority(std::thread& thr, int priority);

}

}
