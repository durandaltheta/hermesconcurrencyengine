//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#ifndef HERMES_COROUTINE_ENGINE_POOL_ALLOCATOR
#define HERMES_COROUTINE_ENGINE_POOL_ALLOCATOR

#include <cstdlib>
#include <type_traits>
#include <algorithm>
#include <limits>
#include <string>
#include <sstream>
#include <vector>

#include "utility.hpp"
#include "logging.hpp"
#include "memory.hpp"

namespace hce {
namespace config {
namespace pool_allocator {

/**
 @brief configures the default block limit of a pool allocator
 */
extern size_t default_block_limit();

}
}

/**
 @brief a private pool allocator

 `pool_allocator` ultimately allocate its values from the thread_local 
 `hce::memory::cache_allocator` via `hce::allocate<T>()`/`hce::deallocate<T>()`
 (which itself allocates via `std::malloc()`).

 `pool_allocator`s are like `hce::memory::cache_allocator`s in concept, with 
 several important distinguishing features:

 - pooled values are completely private to the owner of this object, where-as 
 cached values in the cache_allocator are intended to be used by any caller on
 the thread 
 - pooled values are all of the same size (`sizeof(T)`)
 - `pool_allocator`'s block limit can be precisely calibrated to the caller's 
 exact needs, where-as cache_allocators are configured generally
 - `pool_allocator`s can be used as an `std::` container's allocator

 This object prefers to cache values on deallocation rather than immediately 
 freeing them. It also prefers to retrieve values from its cache over actually 
 allocating memory.

 This object's design is to limit the cost of repeated allocation and 
 deallocation of consistently sized memory blocks. As a note, the primary 
 desired efficiency improvement for using this mechanism is to guarantee a limit 
 to process-wide lock contention from calls to malloc()/free().

 block_limit determines the maximum cache size of the pool. That is, a 
 block_limit of 64 means that a maximum of 64 allocated Ts can be cached 
 for reuse before the allocator is forced to free memory when deallocate() is 
 called.

 The internal pool starts at a size of 0 and grows powers of 2 until a 
 maximum of block_limit is reached.

 This object does *NOT* pool allocations/deallocations of arrays of T. These 
 are deallocated immediately.

 Design Aims:
 - lazy allocated pool growth 
 - private memory pool (safe from global or thread_local shared use)
 - constant time allocation/deallocation when re-using allocated pointers of `T`
 - no exception handling (for speed)
 - usable as an std:: container allocator
 - all memory uses the same underlying allocation/deallocation method, allowing 
 allocation and deallocation from different pools 
 - array allocation/deallocation of `T` uses framework mechanism

 Design Limitations:
 - no default pre-pooling
 - allocated pool can only grow, never shrink
 */
template <typename T>
struct pool_allocator : public printable {
    using value_type = T;
    using pointer = T*;
    using const_pointer = const T*;
    using reference = T&;
    using const_reference = const T&;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;

    /// necessary for rebinding this allocator to different types
    template <typename U>
    struct rebind {
        using other = pool_allocator<U>;
    };

    pool_allocator(size_t block_limit = config::pool_allocator::default_block_limit()) : 
        block_limit_(block_limit)
    { }

    pool_allocator(const pool_allocator<T>& rhs) :
        block_limit_(rhs.block_limit_)
    { 
        HCE_MIN_CONSTRUCTOR(std::string("const ") + rhs.to_string() + "&");
    }

    /**
     Allocator template conversion constructor. Used by std:: containers when 
     they are constructing their own internal objects and want to use this 
     allocator for different types. Allows for allocators of multiple types to 
     have the same block limit.
     */
    template <typename U, typename = std::enable_if_t<!std::is_same_v<T, U>>>
    pool_allocator(const pool_allocator<U>& rhs) : block_limit_(rhs.limit()) {
        // Copy the block limit during rebind
        HCE_MIN_CONSTRUCTOR(std::string("const ") + rhs.to_string() + "&");
    }

    pool_allocator(pool_allocator<T>&& rhs) : block_limit_(0) {
        HCE_MIN_CONSTRUCTOR(rhs.to_string() + "&&");
        block_limit_ = std::move(rhs.block_limit_);
        pool_ = std::move(rhs.pool_);
    }

    ~pool_allocator() {
        HCE_MIN_DESTRUCTOR(); 

        // free all memory
        while(pool_.size()) {
            hce::deallocate<T>(pool_.back(), 1); 
            pool_.pop_back();
        }
    }

    static inline std::string info_name() { 
        return type::templatize<T>("hce::pool_allocator"); 
    }

    inline std::string name() const { return pool_allocator<T>::info_name(); }

    pool_allocator<T>& operator=(const pool_allocator<T>& rhs) {
        HCE_MIN_METHOD_ENTER("operator=", std::string("const ") + rhs.to_string() + "&");
        block_limit_ = rhs.block_limit_;
        return *this;
    }

    pool_allocator<T>& operator=(pool_allocator<T>&& rhs) {
        HCE_MIN_METHOD_ENTER("operator=", rhs.to_string() + "&&");
        block_limit_ = std::move(rhs.block_limit_);
        pool_ = std::move(rhs.pool_);
        return *this;
    }

    /// all pool_allocator<T>s use the global alloc()/free()
    inline bool operator==(const pool_allocator<T>&) { return true; }

    std::size_t max_size() const noexcept {
        return std::numeric_limits<std::size_t>::max() / sizeof(T);
    }
   
    /// allocate a block of memory the size of n*T
    inline T* allocate(std::size_t n) {
        HCE_MIN_METHOD_ENTER("allocate", n);
        T* t;

        if(n==1 && pool_.size()) [[likely]] { 
            // prefer to retrieve from the pool
            t = pool_.back();
            pool_.pop_back();
        } else [[unlikely]] { 
            // allocate memory if necessary
            t = static_cast<T*>(hce::allocate<T>(sizeof(T) * n));
        }

        return t;
    }

    /// deallocate a block of memory
    inline void deallocate(T* t, std::size_t n) {
        if(n==1 && pool_.size() < block_limit_) [[likely]] { 
            // pool the allocated memory for later
            pool_.push_back(t); 
        } else [[unlikely]] { 
            // free memory if necessary
            hce::deallocate<T>(t, n);
        }
    }

    template <typename U, typename... Args>
    void construct(U* p, Args&&... args) {
        ::new (static_cast<void*>(p)) U(std::forward<Args>(args)...);
    }

    template <typename U>
    void destroy(U* p) noexcept {
        p->~U();
    }

    /// return the pool size limit
    inline size_t limit() const { return block_limit_; }

    /// return the pool's available() count
    inline size_t available() const { return pool_.size(); }

    /// return true if the pool is empty, else false
    inline bool empty() const { return pool_.empty(); }

    /// return true if the pool is full, else false
    inline bool full() const { return pool_.size() == block_limit_; }

    bool operator==(const pool_allocator<T>&) const noexcept { return true; }
    bool operator==(const hce::allocator<T>&) const noexcept { return false; }
    bool operator==(const std::allocator<T>&) const noexcept { return false; }
    bool operator!=(const pool_allocator<T>&) const noexcept { return false; }
    bool operator!=(const hce::allocator<T>&) const noexcept { return true; }
    bool operator!=(const std::allocator<T>&) const noexcept { return true; }

private:
    size_t block_limit_;
    std::vector<T*> pool_;
};

}

template <typename T, typename U>
bool operator==(const hce::allocator<T>&, const hce::pool_allocator<U>&) {
    return false;
}

template <typename T, typename U>
bool operator==(const std::allocator<T>&, const hce::pool_allocator<U>&) {
    return false;
}

template <typename T, typename U>
bool operator!=(const hce::allocator<T>& lhs, const hce::pool_allocator<U>& rhs) {
    return !(lhs == rhs);
}

template <typename T, typename U>
bool operator!=(const std::allocator<T>& lhs, const hce::pool_allocator<U>& rhs) {
    return !(lhs == rhs);
}

#endif
