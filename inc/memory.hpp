//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#ifndef __HERMES_COROUTINE_ENGINE_MEMORY__
#define __HERMES_COROUTINE_ENGINE_MEMORY__

#include <cstdlib>
#include <limits>
#include <memory>

#include "utility.hpp"

namespace hce {
namespace config {
namespace memory {
namespace cache {

/**
 @brief cache configuration interface 

 This interface is used by the cache to initialize.
 */
struct info {
    struct bucket {
        const size_t block; //< bucket element block size
        const size_t limit; //< maximum element count for the bucket
    };

    /// return the count of buckets
    virtual size_t count() = 0;

    /// return the bucket info for a given index
    virtual bucket& at(size_t idx) = 0;

    /// index function typedef
    typedef size_t(*index_f)(size_t);

    /**
     Return a function capable of calculating the index of a given bucket based 
     on an input block size. The returned index can be passed to `at()` to 
     select the proper bucket.

     @return the a function which can calculate the index
     */
    virtual index_f indexer() = 0;
};

/// get the info implementation
extern info& get_info();

}
}
}

namespace memory {

/*
 An memory allocator which allows for thread_local caches of deallocated values 
 for reuse on subsequent allocate() calls. This allows for limiting lock 
 contention on process-wide `std::malloc()`/`std::free()`.

 This is not an allocator which can be passed to a container, as it manages 
 multiple block sizes instead of a single templated `T`. However, this 

 This cache is non-exhaustive, std::malloc()/std::free() will be called as 
 necessary.
 */
struct cache { 
    cache() {
        auto& info = config::memory::cache::get_info();
        index_ = info.indexer();

        for(size_t i=0; i<info.count(); ++i) {
            auto& b = info.at(i);
            buckets_.push_back(bucket(b.block, b.limit));
        }
    }

    ~cache() { }

    /// return the thread_local cache
    static cache& get();

    void* allocate(size_t size) {
        size_t idx = index_(size);

        if(idx < buckets_.size()) [[likely]] {
            return buckets_[idx].allocate();
        } else {
            return std::malloc(size);
        }
    }

    void* allocate(size_t alignment, size_t size) {
        return allocate(aligned_size_(alignment, size));
    }

    void deallocate(void* ptr, size_t size) {
        size_t idx = index_(size);

        if(idx < buckets_.size()) [[likely]] {
            buckets_[idx].deallocate(ptr);
        } else {
            std::free(ptr);
        }
    }

    void deallocate(void* ptr, size_t alignment, size_t size) {
        deallocate(ptr, aligned_size_(alignment, size));
    }

private:
    struct bucket {
        bucket(size_t b, size_t l) : block(b), limit(l) { }

        ~bucket() {
            for(auto ptr : free_list) {
                std::free(ptr);
            }
        }

        // allocate a chunk of memory from the bucket
        void* allocate() {
            if (free_list.empty()) {
                return std::malloc(block);
            } else {
                --free_count;
                void* ptr = free_list.back();
                free_list.pop_back();
                return ptr;
            }
        }

        // free a chunk of memory
        void deallocate(void* ptr) {
            if(free_count >= limit) [[unlikely]] {
                std::free(ptr);
            } else [[likely]] {
                ++free_count;
                free_list.push_back(ptr);
            }
        }

        const size_t block;
        const size_t limit;
        size_t free_count = 0; // count of cached elements in the free list
        std::vector<void*> free_list; // cached, deallocated values
    };

    // bitwise aligned size calculation
    size_t aligned_size_(size_t alignment, size_t size) {
        return (size + alignment - 1) & ~(alignment - 1);
    }

    // bucket index calculation function
    hce::config::memory::cache::info::index_f index_;

    // the various buckets managing allocations of different sized blocks
    std::vector<bucket> buckets_;
};

/**
 @return an allocated pointer of at least `size` bytes
 */
inline void* allocate(size_t size) {
    return hce::memory::cache::get().allocate(size);
}

/**
 @return an aligned allocated pointer of at least `size` bytes
 */
inline void* allocate(size_t alignment, size_t size) {
    return hce::memory::cache::get().allocate(alignment, size);
}

/**
 @brief deallocate a pointer of `size` bytes
 */
inline void deallocate(void* p, size_t size) {
    return hce::memory::cache::get().deallocate(p, size);
}

/**
 @brief deallocate a pointer of aligned `size` bytes
 */
inline void deallocate(void* p, size_t alignment, size_t size) {
    return hce::memory::cache::get().deallocate(p, alignment, size);
}

}

/**
 @brief high level, alignment aware memory allocation for template type T
 @param count of element Ts in allocated block 
 @return allocated pointer
 */
template <typename T>
inline T* allocate(size_t n=1) {
    return reinterpret_cast<T*>(memory::allocate(alignof(T), sizeof(T) * n));
}

/**
 @brief high level deallocate `hce::allocate<T>()`ed memory 

 Memory `p` can be optionally deleted by regular `delete` or `std::free()`. The 
 advantage of using this mechanism is that the implementation of 
 `memory::deallocate()` can cache allocated values in a thread_local mechanism 
 for reuse on calls to `memory::allocate()`. 

 @param p pointer to memory
 @param n size passed to allocate()
 */
template <typename T>
inline void deallocate(void* p, size_t n=1) { 
    memory::deallocate(p, alignof(T), sizeof(T) * n);
}

/**
 @brief std:: compliant allocator that makes use of thread_local caching hce::allocate<T>/hce::deallocate<T> 

 Because this object uses a cache, which itself uses `malloc()`/
 `free()`, it is compatible with `std::allocator<T>`.

 Design Aims:
 - Provide an allocator as close to std::allocator's design as possible 
 - Utilize thread_local allocation without overriding global new/delete 
 - constant time allocation/deallocation when re-using allocated blocks
 - no exception handling (for speed)
 - usable as an std:: container allocator
 - all memory uses the same underlying allocation/deallocation method

 Design Limitations:
 - no default pre-caching 
 - relies on predefined block cache size limits within hce::allocate()/
 hce::deallocate() mechanisms (no resizing or non-bucket size optimizations)
 - underlying mechanism's caches can only grow, never shrink
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
    allocator<T>& operator=(const allocator<U>& rhs) { 
        return *this;
    }

    template <typename U>
    allocator<T>& operator=(allocator<U>&& rhs) {
        return *this;
    }

    std::size_t max_size() const noexcept {
        return std::numeric_limits<std::size_t>::max() / sizeof(T);
    }
   
    /// allocate a block of memory the size of T * n
    inline T* allocate(std::size_t n) { 
        return hce::allocate<T>(n); 
    }

    /**
     @brief deallocate a block of memory 

     It is an error for the user to pass a pointer to this function that was 
     not acquired from allocate() by the same allocator
     */
    inline void deallocate(T* t, std::size_t n) {
        hce::deallocate<T>(t,n); 
    }

    template <typename... Args>
    void construct(T* p, Args&&... args) {
        ::new (static_cast<void*>(p)) T(std::forward<Args>(args)...);
    }

    void destroy(T* p) noexcept {
        p->~U();
    }


    // comparison operators (required for allocator compatibility)
    bool operator==(const allocator<T>&) const noexcept { return true; }

    // this allocator can deallocate standard allocations and vice-versa
    bool operator==(const std::allocator<T>&) const noexcept { return true; }

    bool operator!=(const allocator<T>&) const noexcept { return false; }
    bool operator!=(const std::allocator<T>&) const noexcept { return false; }
};


namespace memory {

/// std::unique_ptr deleter function template
template <typename T, size_t sz>
inline void deleter(T* p) {
    for(size_t i=0; i<sz; ++i) {
        (p[i]).~T();
    }
    
    hce::deallocate<T>(p,sz);
}

template <typename T>
inline void deleter(T* p);

/// specialization for single element (sz = 1)
template <typename T>
inline void deleter(T* p) {
    p->~T(); // call the destructor for the single element
    hce::deallocate<T>(p, 1); 
}

}

/**
 @brief allocate a unique_ptr which deletes using the framework allocation/deleter 

 Failing to uses this or to implement the underlying logic means that when the 
 unique_ptr goes out of scope the allocated memory will be directly be passed 
 to `std::free()`, rather than be potentially cached for reuse.

 @param as... T constructor arguments 
 @return an allocated `std::unique_ptr<T>`
 */
template <typename T, typename... Args>
auto make_unique(Args&&... args) {
    T* t = hce::allocate<T>(1);
    new(t) T(std::forward<Args>(args)...);
    return std::unique_ptr<T>(t, &memory::deleter<T>);
}

/**
 @brief allocate and construct an std::function<R()> from a callable and optional arguments 

 The constructed std::function will return the result of the argument callable.

 @param callable a Callable
 @param args... any arguments to be bound to callable 
 @return an allocated std::unique_ptr containing the hce::thunk
 */
template <typename Callable, typename... Args>
auto make_unique_callable(Callable&& callable, Args&&... args) {
    using FunctionType = std::function<std::result_of_t<Callable(Args...)>()>;

    FunctionType* ft = hce::allocate<FunctionType>(1);

    new(ft) FunctionType(
        [callable = std::forward<Callable>(callable),
         ...args = std::forward<Args>(args)]() mutable {
            return callable(args...);
        });

    return std::unique_ptr<FunctionType>(ft, &memory::deleter<FunctionType>);
}

/**
 @brief allocate and construct a thunk from a callable and optional arguments 

 An hce::thunk is a Callable which accepts no arguments and returns void.

 @param callable a Callable
 @param args... any arguments to be bound to callable 
 @return an allocated std::unique_ptr containing the hce::thunk
 */
template <typename Callable, typename... Args>
auto make_unique_thunk(Callable&& callable, Args&&... args) {
    hce::thunk* th = hce::allocate<hce::thunk>(1);

    new(th) hce::thunk(
        [callable = std::forward<Callable>(callable),
         ...args = std::forward<Args>(args)]() mutable {
            callable(args...);
        });

    return std::unique_ptr<hce::thunk>(th, &memory::deleter<hce::thunk>);
}

}

template <typename T, typename U>
bool operator==(const std::allocator<T>&, const hce::allocator<U>&) {
    return true;
}

template <typename T, typename U>
bool operator!=(const std::allocator<T>& lhs, const hce::allocator<U>& rhs) {
    return !(lhs == rhs);
}

#endif
