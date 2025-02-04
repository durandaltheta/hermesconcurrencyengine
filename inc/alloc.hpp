//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
/**
 @file custom allocation management for the framework 

 Builds on top of memory cache implementation, adding template typing, smart 
 pointer consideratinos and container aware allocators.

 This is the header most code needs to include.
 */
#ifndef HERMES_COROUTINE_ENGINE_ALLOC
#define HERMES_COROUTINE_ENGINE_ALLOC

#include <type_traits>
#include <string>
#include <sstream>
#include <vector>

#include "utility.hpp"
#include "logging.hpp"
#include "memory.hpp"

namespace hce {

/**
 @brief bitwise aligned size calculation
 @param n the count of elements T
 @return the alignment adjusted size for all elements T
 */
template <typename T>
inline std::size_t aligned_size(std::size_t n) {
    return ((sizeof(T) * n) + alignof(T) - 1) & ~(alignof(T) - 1);
}

/**
 @brief high level, alignment aware memory allocation for template type T
 @param count of element Ts in allocated block 
 @return allocated pointer
 */
template <typename T>
inline T* allocate(std::size_t n=1) {
    return reinterpret_cast<T*>(hce::memory::allocate(aligned_size<T>(n)));
}

/**
 @brief high level, alignment aware, `hce::allocate<T>()`ed memory deallocation

 Memory `p` can be optionally deleted by regular `delete` or `std::free()`. The 
 advantage of using this mechanism is that the implementation of 
 `memory::deallocate()` can cache allocated values in a thread_local mechanism 
 for reuse on calls to `memory::allocate()`. 

 @param p pointer to allocated memory
 */
template <typename U>
inline void deallocate(U* p) { 
    hce::memory::deallocate((void*)p);
}

/**
 @brief std:: compliant allocator that makes use of thread_local caching hce::allocate<T>/hce::deallocate

 Design Aims:
 - written as close to std::allocator's design as possible 
 - utilize thread_local allocation cache without overriding global new/delete 
 - constant time allocation/deallocation when re-using allocated blocks
 - no exception handling (for speed)
 - usable as an std:: container allocator

 Design Limitations:
 - no default pre-caching 
 - memory cannot be deallocated with std::free()
 - relies on predefined block cache size limits within hce::allocate<T>()/
 hce::deallocate() mechanisms (no resizing or non-bucket size optimizations)
 */
template <typename T>
struct allocator {
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
        using other = allocator<U>;
    };

    allocator() { }

    template <typename U>
    allocator(const allocator<U>& rhs) { }

    template <typename U>
    allocator(allocator<U>&& rhs) { }

    ~allocator() { }

    template <typename U>
    inline allocator<T>& operator=(const allocator<U>& rhs) { 
        return *this;
    }

    template <typename U>
    inline allocator<T>& operator=(allocator<U>&& rhs) {
        return *this;
    }

    inline std::size_t max_size() const noexcept {
        return std::numeric_limits<std::size_t>::max() / sizeof(T);
    }
   
    /// allocate a block of memory the size of T * n
    inline T* allocate(std::size_t n) { 
        return hce::allocate<T>(n); 
    }

    /**
     @brief deallocate a block of memory 

     The argument `n` must be the same size as passed to `allocate()` (or 
     `std::malloc()` if allocated directly).
     */
    inline void deallocate(T* t, std::size_t n) {
        hce::deallocate(t); 
    }

    template <typename... Args>
    inline void construct(T* p, Args&&... args) {
        ::new (static_cast<void*>(p)) T(std::forward<Args>(args)...);
    }

    inline void destroy(T* p) noexcept {
        p->~U();
    }

    // comparison operators (required for allocator compatibility)
    inline bool operator==(const allocator<T>&) const noexcept { return true; }

    // this allocator can deallocate standard allocations and vice-versa
    inline bool operator==(const std::allocator<T>&) const noexcept { return false; }

    inline bool operator!=(const allocator<T>&) const noexcept { return false; }
    inline bool operator!=(const std::allocator<T>&) const noexcept { return true; }
};

namespace alloc {

/// std::unique_ptr deleter function template
template <typename T, std::size_t sz>
struct deleter {
    inline void operator()(T* p) const {
        for(std::size_t i=0; i<sz; ++i) [[likely]] {
            (p[i]).~T();
        }
        
        hce::deallocate(p);
    }
};


/// specialization for single element (sz = 1)
template <typename T>
struct deleter<T,1> {
    inline void operator()(T* p) const {
        p->~T(); // call the destructor for the single element
        hce::deallocate(p); 
    }
};

}

/**
 @brief unique_ptr type enforcing cache deallocation 
 
 The allocated `T` must be constructed with an aligned size. IE, 
 `hce::allocate<T>(...)`. Failure to do this an error.
 */
template <typename T>
using unique_ptr = std::unique_ptr<T,alloc::deleter<T,1>>;

/**
 @brief allocate an hce::unique_ptr<T> which deletes using the framework deleter 

 Failing to uses this mechanism or to implement the underlying logic means that 
 when the unique_ptr goes out of scope the allocated memory will be directly be 
 passed to `std::free()`, rather than be potentially cached for reuse, which is 
 not an error but may be less efficient.

 @param as... T constructor arguments 
 @return an allocated `std::unique_ptr<T>`
 */
template <typename T, typename... Args>
auto make_unique(Args&&... args) {
    T* t = hce::allocate<T>(1);
    new(t) T(std::forward<Args>(args)...);
    return hce::unique_ptr<T>(t);
}

/**
 @brief allocate a shared_ptr which deletes using the framework deleter

 @param as... T constructor arguments 
 @return an allocated `std::shared_ptr<T>`
 */
template <typename T, typename... Args>
auto make_shared(Args&&... args) {
    T* t = hce::allocate<T>(1);
    new(t) T(std::forward<Args>(args)...);
    return std::shared_ptr<T>(t,alloc::deleter<T,1>());
}

namespace alloc {

/**
 @brief construct an allocated std::function<R()> pointer from a callable and arguments 
 @param callable a Callable
 @param args... any arguments to be bound to callable 
 */
template <typename Callable, typename... Args>
inline void construct_callable_ptr(
        std::function<std::invoke_result_t<Callable, Args...>()>* ft, 
        Callable&& callable, 
        Args&&... args) 
{
    using FunctionType = std::function<std::invoke_result_t<Callable, Args...>()>;

    new(ft) FunctionType(
        [callable = std::forward<Callable>(callable),
         ...args = std::forward<Args>(args)]() mutable {
            return callable(args...);
        });
}

/**
 @brief construct an allocated thunk pointer from a callable and arguments 
 @param callable a Callable
 @param args... any arguments to be bound to callable 
 */
template <typename Callable, typename... Args>
inline void construct_thunk_ptr(hce::thunk* th, Callable&& callable, Args&&... args) {
    new(th) hce::thunk(
        [callable = std::forward<Callable>(callable),
         ...args = std::forward<Args>(args)]() mutable {
            callable(args...);
        });
}

}

/**
 @brief allocate and construct a hce::unique_ptr of std::function<R()> from a callable and optional arguments 

 The constructed std::function will return the result of the argument callable 
 with the provided arguments.

 @param callable a Callable
 @param args... any arguments to be bound to callable 
 @return an allocated unique_ptr to the std::function<R()>
 */
template <typename Callable, typename... Args>
auto make_unique_callable(Callable&& callable, Args&&... args) {
    using FunctionType = std::function<std::invoke_result_t<Callable, Args...>()>;

    FunctionType* ft = hce::allocate<FunctionType>(1);

    alloc::construct_callable_ptr(
        ft,
        std::forward<Callable>(callable), 
        std::forward<Args>(args)...);

    return hce::unique_ptr<FunctionType>(ft);
}

/**
 @brief allocate and construct a thunk from a callable and optional arguments 

 An hce::thunk is a Callable which accepts no arguments and returns void. The 
 constructed will execute the callable with the provided arguments.

 @param callable a Callable
 @param args... any arguments to be bound to callable 
 @return an allocated hce_unique_ptr to the hce::thunk
 */
template <typename Callable, typename... Args>
auto make_unique_thunk(Callable&& callable, Args&&... args) {
    hce::thunk* th = hce::allocate<hce::thunk>(1);

    alloc::construct_thunk_ptr(
        th, 
        std::forward<Callable>(callable), 
        std::forward<Args>(args)...);

    return hce::unique_ptr<hce::thunk>(th);
}

namespace config {
namespace pool_allocator {

/**
 @brief configures the default block limit of a pool allocator
 */
size_t default_block_limit();

}
}

/**
 @brief a pool allocator

 `pool_allocator` ultimately allocate its values from the thread_local 
 memory caches via `hce::allocate<T>()`/`hce::deallocate()`
 (which itself allocates via `std::malloc()`). It is therefore compatible with 
 `hce::allocator`.

 `pool_allocator`s are like memory caches in concept, with 
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
 allocation and deallocation from different pool_allocators 
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

    /// construct a pool_allocator with the specified block_limit
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

    virtual ~pool_allocator() {
        HCE_MIN_DESTRUCTOR(); 

        // free all memory
        while(pool_.size()) {
            hce::deallocate(pool_.back()); 
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
            t = hce::allocate<T>(sizeof(T) * n);
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
            hce::deallocate(t);
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
bool operator==(const std::allocator<T>&, const hce::allocator<U>&) {
    return false;
}

template <typename T, typename U>
bool operator!=(const std::allocator<T>& lhs, const hce::allocator<U>& rhs) {
    return !(lhs == rhs);
}

template <typename T, typename U>
bool operator==(const hce::allocator<T>&, const hce::pool_allocator<U>&) {
    return true;
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
