//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#ifndef __HERMES_COROUTINE_ENGINE_POOL_ALLOCATOR__
#define __HERMES_COROUTINE_ENGINE_POOL_ALLOCATOR__ 

#include <cstdlib>
#include <algorithm>
#include <string>
#include <sstream>

#include "utility.hpp"
#include "logging.hpp"
#include "memory.hpp"
#include "circular_buffer.hpp"

#ifndef HCEPOOLALLOCATORDEFAULTSIZELIMIT
#define HCEPOOLALLOCATORDEFAULTSIZELIMIT 64
#endif

namespace hce {

/**
 @brief a pool allocator

 Memory in a pool allocator is not guaranteed to be contiguous.

 This object prefers to cache values on deallocation rather than immediately 
 freeing them. It also prefers to retrieve values from its cache over actually 
 allocating memory.

 This object's design is to limit the cost of repeated allocation and 
 deallocation of consistently sized memory blocks. As a note, the primary 
 desired efficiency improvement for using this mechanism is to guarantee a limit 
 to process-wide lock contention from calls to malloc()/free().

 pool_size_limit determines the maximum cache size of the pool. That is, a 
 pool_size_limit of 64 means that a maximum of 64 allocated Ts can be cached 
 for reuse before the allocator is forced to free memory when deallocate() is 
 called.

 The cache's pool starts at a size() of 0 and grows by a power of 2 until a 
 maximum of pool_size_limit is reached.

 This object does *NOT* cache allocations/deallocations of arrays of T. These 
 are malloc()ed/free()ed immediately.
 */
template <typename T>
struct pool_allocator : public printable {
    using value_type = T;
    static constexpr size_t pool_default_size_limit = 
        HCEALLOCATORDEFAULTSIZELIMIT;

    pool_allocator(size_t pool_size_limit = pool_default_size_limit) : 
        pool_size_limit_(pool_size_limit),
        pool_(0)
    { }

    /**
     This variant is used to ensure that the pool pre-caches memory rather than
     growing to the limit over time.
     */
    pool_allocator(size_t pool_size_limit, hce::pre_cache) : 
        pool_size_limit_(pool_size_limit),
        pool_(pool_size_limit_) // construct the pool at the maximum size
    { 
        while(!pool_.full()) { pool_.push(hce::allocate<T>(1); }
    }

    pool_allocator(const pool_allocator<T>& rhs) :
        pool_size_limit_(rhs.pool_size_limit_),
        pool_(0) // buffers are never copied 
    { }

    pool_allocator(pool_allocator<T>&&) = default;

    ~pool_allocator() {
        // free all memory
        while(pool_.used()) {
            hce::deallocate(pool_.front()); 
            pool_.pop();
        }
    }

    static inline std::string info_name() { 
        return type::templatize<T>("hce::pool_allocator"); 
    }

    inline std::string name() const { return pool_allocator<T>::info_name(); }

    inline std::string content() const {
        std::stringstream ss;
        ss << "limit:" << limit() << ", size:" << size() << ", used:" << used();
        return ss.str();
    }

    pool_allocator<T>& operator=(const pool_allocator<T>&) {
        pool_size_limit_ = rhs.pool_size_limit_;
        pool_ = 0; // buffers are never copied 
        return *this;
    }

    pool_allocator<T>& operator=(pool_allocator<T>&&) = default;

    /// all pool_allocator<T>s use the global alloc()/free()
    inline bool operator==(const pool_allocator<T>&) { return true; }
   
    /// allocate a block of memory the size of n*T
    inline T* allocate(size_t n) {
        HCE_MIN_METHOD_ENTER("allocate", n);
        T* t;

        if(n==1 && !pool_.empty()) [[likely]] { 
            // prefer to retrieve from the cache
            t = pool_.front();
            pool_.pop();
        } else [[unlikely]] { 
            // allocate memory if necessary
            t = hce::allocate<T>(n);
        }

        return t;
    }

    /// deallocate a block of memory
    inline void deallocate(T* t, size_t n) {
        if(n==1 && (!pool_.full() || grow_pool_())) [[likely]] { 
            // cache the allocated memory for later
            pool_.push(t); 
        } else [[unlikely]] { 
            // free memory if necessary
            hce::deallocate(t); 
        }
    }

    /// return the pool size limit
    inline size_t limit() const { return pool_size_limit_; }

    /// return the pool's current size
    inline size_t size() const { return pool_.size(); }

    /// return the pool's used() count
    inline size_t used() const { return pool_.used(); }

    /// return the pool's remaining() count
    inline size_t remaining() const { return pool_.remaining(); }

    /// return true if the pool is empty, else false
    inline bool empty() const { return pool_.empty(); }

    /// return true if the pool is full, else false
    inline bool full() const { return pool_.full(); }

private:
    /*
     Grow the pool to accommodate more values. The design of this function is 
     intended to emulate `std::vector` amortized growth, the caveat that the 
     size of the pool is hard limited by the templated pool_size_limit_.

     Returns true if the pool grew, else false
     */
    inline bool grow_pool_() {
        if(pool_.size() < pool_size_limit_) {
            const std::size_t new_size = std::min(
                pool_size_limit_, 
                pool_.size() ? pool_.size() * 2 : 1);
            hce::circular_buffer<T*> new_pool(new_size);

            // copy everything over from the old to new pool
            while(pool_.used()) [[likely]] {
                // copyng is faster for pointers than moving, and there's no 
                // need to consider RAII semantics with pointers
                new_pool.push(pool_.front());
                pool_.pop();
            }

            // swap the pools
            pool_ = std::move(new_pool);
            return true;
        } else {
            return false; 
        }
    }

    size_t pool_size_limit_;
    hce::circular_buffer<T*> pool_; // circular_buffer of pooled memory
};

}

#endif
