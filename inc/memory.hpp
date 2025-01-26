//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#ifndef HERMES_COROUTINE_ENGINE_MEMORY
#define HERMES_COROUTINE_ENGINE_MEMORY

#include <cstdlib>
#include <limits>
#include <memory>

#include "base.hpp"
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
    /// runtime calling thread information
    struct thread {
        enum type {
            system, //< a regular system thread
            global, //< the thread designated for the global hce::scheduler
            scheduler //< a thread designated for an arbitrary hce::scheduler
        };

        /// get the calling thread's type
        static type& get_type();
    };

    struct bucket {
        const std::size_t block; //< bucket element block size
        const std::size_t limit; //< maximum element count for the bucket
    };

    /// get the info implementation
    static info& get();

    /// return the count of buckets
    virtual std::size_t count() const = 0;

    /// return the bucket info for a given index
    virtual bucket& at(std::size_t idx) = 0;

    /**
     An index function is capable of calculating the index of a given bucket 
     based on an input block size. The returned index can be passed to `at()` to 
     select the proper bucket that contains *at least* the requested block size.
     */
    typedef std::size_t(*index_function)(const std::size_t);

    /**
     @return the index function which can calculate the index based on argument block size
     */
    virtual index_function indexer() = 0;
};

}
}
}

namespace memory {

/*
 A memory allocation mechanism which allows for thread_local caches of 
 deallocated values for reuse on subsequent allocate() calls. This allows for 
 limiting lock contention on process-wide `std::malloc()`/`std::free()`.

 This is not an allocator which can be passed to a container, as it manages 
 multiple block sizes instead of a single templated `T`. It is instead a 
 mechanism for other allocation mechanisms to build on top of.

 This cache is non-exhaustive, std::malloc()/std::free() will be called as 
 necessary.

 WARNING: Allocations from this mechanism cannot be directly deallocated by 
 `std::free()` or `delete`, because the allocated values contain a header which 
 located at an address *before* the returned allocated pointer.
 */
struct cache { 
    struct bad_size_alloc : public std::exception {
        inline const char* what() const noexcept { 
            return "hce::memory::cache: cannot allocate block size of 0"; 
        }
    };

    struct bad_dealloc : public std::exception {
        inline const char* what() const noexcept { 
            return "hce::memory::cache: cannot deallocate a nullptr"; 
        }
    };

    struct bad_size_dealloc : public std::exception {
        inline const char* what() const noexcept { 
            return "hce::memory::cache: cannot allocate block size of 0"; 
        }
    };

    cache() {
        // acquire the config::memory::cache::info implementation
        auto& info = config::memory::cache::info::get();

        // acquire the index() function
        index_ = info.indexer();

        // allocate enough memory for all buckets at once
        buckets_.reserve(info.count());

        // setup the cache buckets based on the configuration
        for(std::size_t i=0; i<info.count(); ++i) {
            config::memory::cache::info::bucket& b = info.at(i);
            buckets_.push_back(bucket(i, b.block, b.limit));
        }
    }

    ~cache() { }

    /**
     The cache for the main thread is a static global (enabling use during 
     non-global object destructors at process shutdown). Additional threads use 
     `thread_local` caches.

     @return the thread's assigned cache
     */
    static cache& get();

    inline void* allocate(const std::size_t size) {
        if(!size) [[unlikely]] { throw bad_size_alloc(); }

        const std::size_t index = index_(size);

        if(index < buckets_.size()) [[likely]] {
            return cache::from_header(buckets_[index].allocate());
        } else [[unlikely]] {
            return cache::from_header(cache::allocate_header(size, index));
        }
    }

    inline void deallocate(const void* ptr) {
        if(!ptr) [[unlikely]] { throw bad_dealloc(); }

        // Get the header which is before the address of `ptr`. The header is 
        // the actual address which needs to be deallocated
        header* hdr = cache::to_header(ptr);
        const std::size_t index = hdr->index;

        if(index < buckets_.size()) [[likely]] {
            buckets_[index].deallocate(hdr);
        } else [[unlikely]] {
            cache::deallocate_header(hdr);
        }
    }

    // return the bucket count
    inline std::size_t count() const {
        return buckets_.size();
    }

    // return the bucket index for a given allocation size
    inline std::size_t index(std::size_t size) const {
        return index_(size);
    }

    // return the count of available cached allocations for a given size
    inline std::size_t available(std::size_t size) const {
        const std::size_t index = index_(size);

        if(index < buckets_.size()) [[likely]] {
            return buckets_[index].free_count;
        } else [[unlikely]] {
            return 0;
        }
    }

    // return the max count of available cached allocations for a given size
    inline std::size_t limit(std::size_t size) const {
        std::size_t idx = index_(size);

        if(idx < buckets_.size()) [[likely]] {
            return buckets_[idx].limit;
        } else [[unlikely]] {
            return 0;
        }
    }
   
    // deallocate all memory in the cache
    inline void clear() {
        for(auto& bkt : buckets_) {
            bkt.clear();
        }
    }

private:
    // memory block allocations have this before their beginning address
    struct header {
        unsigned char index; // index of the source bucket
    };

    // allocate a block of memory with a header
    static inline header* allocate_header(const std::size_t size, const unsigned char index) {
        header* hdr = (header*)std::malloc(sizeof(header) + size);
        hdr->index = index; // set the header value for deallocate() lookup
        return hdr;
    }

    // deallocate a block of memory with a header
    static inline void deallocate_header(header* hdr) {
        std::free(hdr);
    }

    // acquire the header pointer from the allocated block
    static inline header* to_header(const void* ptr) {
        return (header*)((char*)ptr - sizeof(header));
    }

    // acquire the allocated block from the header pointer
    static inline void* from_header(const header* hdr) {
        return (void*)((char*)hdr + sizeof(header));
    }

    struct bucket {
        bucket(unsigned char i, std::size_t s, std::size_t l) : 
            index(i),
            block(s), 
            limit(l) 
        { }

        ~bucket() { clear(); }

        // allocate a chunk of memory from the bucket
        inline header* allocate() {
            if (free_list.empty()) {
                return cache::allocate_header(block, index);
            } else {
                --free_count;
                header* hdr = free_list.back();
                free_list.pop_back();
                return hdr;
            }
        }

        // free a chunk of memory
        inline void deallocate(header* hdr) {
            if(free_count >= limit) [[unlikely]] {
                cache::deallocate_header(hdr);
            } else [[likely]] {
                // cache the pointer for reuse
                ++free_count;
                free_list.push_back(hdr);
            }
        }

        // free all allocated memory
        inline void clear() {
            while(!(free_list.empty())) {
                cache::deallocate_header(free_list.back());
                free_list.pop_back();
            }

            free_count = 0;
        }

        const unsigned char index; // the index of this bucket in the vector
        const std::size_t block; // the block memory size
        const std::size_t limit; // the number of blocks this bucket can hold
        std::size_t free_count = 0; // count of cached elements in the free list

        /*
         Cached, deallocated values. Vectors are good for this sort of thing 
         because they only re-allocate their underlying memory block if it's 
         too small. If a push won't use up all the remaining memory, it won't 
         allocate. Pops also won't cause re-allocation, because vectors don't 
         contract (re-allocate smaller) in that way.
         */
        std::vector<header*> free_list; 
    };

    /*
     Bucket index calculation function. It accepts a memory block size and 
     returns the index of the bucket that contains at least that size.

     If this returns an index greater than available in the bucket vector, 
     that means the requested block size is larger than available in the cache 
     and must be directly `std::malloc()`ed/`std::free()`ed.
     */
    hce::config::memory::cache::info::index_function index_;

    // the various buckets managing allocations of different block sizes
    std::vector<bucket> buckets_;

    // the pointer to the cache for the main thread
    static cache* main_cache_;

    friend hce::lifecycle;
};

/**
 @brief allocate a pointer of `size` bytes
 @param size the size of the requested allocation
 @return an allocated pointer of at least `size` bytes
 */
inline void* allocate(std::size_t size) {
    return hce::memory::cache::get().allocate(size);
}

/**
 @brief deallocate a pointer of `size` bytes
 @param p the allocated pointer 
 @param size the size that the pointer was `memory::allocate()` with
 */
inline void deallocate(void* p) {
    return hce::memory::cache::get().deallocate(p);
}

/**
 @brief bitwise aligned size calculation
 @param n the count of elements T
 @return the alignment adjusted size for all elements T
 */
template <typename T>
inline std::size_t aligned_size(std::size_t n) {
    return ((sizeof(T) * n) + alignof(T) - 1) & ~(alignof(T) - 1);
}

}

/**
 @brief high level, alignment aware memory allocation for template type T
 @param count of element Ts in allocated block 
 @return allocated pointer
 */
template <typename T>
inline T* allocate(std::size_t n=1) {
    return reinterpret_cast<T*>(hce::memory::allocate(memory::aligned_size<T>(n)));
}

/**
 @brief high level, alignment aware, `hce::allocate<T>()`ed memory deallocation

 Memory `p` can be optionally deleted by regular `delete` or `std::free()`. The 
 advantage of using this mechanism is that the implementation of 
 `memory::deallocate()` can cache allocated values in a thread_local mechanism 
 for reuse on calls to `memory::allocate()`. 

 @param p pointer to allocated memory
 @param n T element count passed to allocate<T>()
 */
template <typename T, typename U>
inline void deallocate(U* p) { 
    hce::memory::deallocate((void*)p);
}

/**
 @brief std:: compliant allocator that makes use of thread_local caching hce::allocate<T>/hce::deallocate<T> 

 Because this object exclusively uses hce::memory::cache, which itself uses 
 `std::malloc()`/`std::free()`, it is compatible with `std::allocator<T>`.

 Design Aims:
 - written as close to std::allocator's design as possible 
 - utilize thread_local allocation cache without overriding global new/delete 
 - constant time allocation/deallocation when re-using allocated blocks
 - no exception handling (for speed)
 - usable as an std:: container allocator
 - all memory uses the default allocation/deallocation methods when necessary

 Design Limitations:
 - no default pre-caching 
 - relies on predefined block cache size limits within hce::allocate<T>()/
 hce::deallocate<T>() mechanisms (no resizing or non-bucket size optimizations)
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

     The argument `n` must be the same size as passed to `allocate()` (or 
     `std::malloc()` if allocated directly).
     */
    inline void deallocate(T* t, std::size_t n) {
        hce::deallocate<T>(t); 
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
    bool operator==(const std::allocator<T>&) const noexcept { return false; }

    bool operator!=(const allocator<T>&) const noexcept { return false; }
    bool operator!=(const std::allocator<T>&) const noexcept { return true; }
};

namespace memory {

/// std::unique_ptr deleter function template
template <typename T, std::size_t sz>
struct deleter {
    void operator()(T* p) const {
        for(std::size_t i=0; i<sz; ++i) [[likely]] {
            (p[i]).~T();
        }
        
        hce::deallocate<T>(p);
    }
};


/// specialization for single element (sz = 1)
template <typename T>
struct deleter<T,1> {
    void operator()(T* p) const {
        p->~T(); // call the destructor for the single element
        hce::deallocate<T>(p); 
    }
};

}

/**
 @brief unique_ptr type enforcing cache deallocation 
 
 The allocated `T` must be constructed with an aligned size. IE, 
 `hce::allocate<T>(...)`. Failure to do this an error.
 */
template <typename T>
using unique_ptr = std::unique_ptr<T,memory::deleter<T,1>>;

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
    return std::shared_ptr<T>(t,memory::deleter<T,1>());
}

namespace memory {

/**
 @brief construct an allocated std::function<R()> pointer from a callable and arguments 
 @param callable a Callable
 @param args... any arguments to be bound to callable 
 */
template <typename Callable, typename... Args>
inline void construct_callable_ptr(
        std::function<std::result_of_t<Callable(Args...)>()>* ft, 
        Callable&& callable, 
        Args&&... args) 
{
    using FunctionType = std::function<std::result_of_t<Callable(Args...)>()>;

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
    using FunctionType = std::function<std::result_of_t<Callable(Args...)>()>;

    FunctionType* ft = hce::allocate<FunctionType>(1);

    memory::construct_callable_ptr(
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

    memory::construct_thunk_ptr(
        th, 
        std::forward<Callable>(callable), 
        std::forward<Args>(args)...);

    return hce::unique_ptr<hce::thunk>(th);
}

}

template <typename T, typename U>
bool operator==(const std::allocator<T>&, const hce::allocator<U>&) {
    return false;
}

template <typename T, typename U>
bool operator!=(const std::allocator<T>& lhs, const hce::allocator<U>& rhs) {
    return !(lhs == rhs);
}

#endif
