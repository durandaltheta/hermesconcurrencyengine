//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
/**
 @file custom memory caches for the framework 

 This implementation is extractible from the greater framework. If the user 
 provides valid `hce::config::memory::cache::info` `get()`/`set()`/`indexer()`
 implementations as well as a `hce::memory::cache::get()` implementation, then 
 they can use this code arbitrarily.
 */
#ifndef HERMES_COROUTINE_ENGINE_MEMORY
#define HERMES_COROUTINE_ENGINE_MEMORY

#include <cstdlib>
#include <limits>
#include <memory>
#include <exception>
#include <vector>

/**
 @brief this type is used for the index of buckets in the memory cache 

 The default is very small, an 8 bit unsigned int (max value == 255). This is 
 generally fine because buckets in the default memory implementation represent 
 block sizes that are powers of 2, which means there cannot be very many 
 buckets (default bucket count == 13).

 Additionally, this is set so small because every allocation in the caches have 
 a header that contains this value, so an overlarge size could potentially waste 
 allocated memory.

 However, if the user needs to implement a custom cache implementation with 
 more buckets than this type can represent, this library and any user code 
 should be recompiled with a defined type which can hold the necessary bucket 
 count.
 */
#ifndef HCEMEMORYCACHEINDEXTYPE
#define HCEMEMORYCACHEINDEXTYPE uint8_t
#endif 

namespace hce {
namespace config {
namespace memory {
namespace cache {

/**
 @brief cache configuration interface 

 This interface is used by the cache to initialize.
 */
struct info {
    typedef HCEMEMORYCACHEINDEXTYPE index_t;

    /**
     @brief describes an individual bucket in a cache
     */
    struct bucket {
        bucket(const std::size_t b, const std::size_t l) :
            block(b),
            limit(l)
        { }

        const std::size_t block; //< bucket element block size
        const std::size_t limit; //< maximum element count for the bucket
    };

    /**
     @brief get the `info` implementation

     Unless `set()` is called, this returns a default `info` implementation. 
     This method is used during construction of `hce::memory::cache`s.

     @return the configured `info` implementation
     */
    static info& get();
   
    /**
     @brief optionally set the info implementation for the calling thread that will be returned by `get()`

     When an `hce::memory::cache` is first constructed it calls 
     `hce::config::memory::cache::info::get()` during it's constructor and use 
     it to configure itself as described. Thus, if a calling thread specifies 
     an `info` implementation with `set()` before the `thread_local` `cache` is
     is first accessed, they can configure the `cache`.
     */
    static void set(info& i);

    virtual const char* name() const = 0;

    /// return the count of buckets
    virtual std::size_t count() const = 0;

    /// return the bucket info for a given index
    virtual bucket& at(std::size_t idx) = 0;

    /**
     An index function is capable of calculating the index of a given bucket 
     based on an input block size. The returned index can be passed to `at()` to 
     select the proper bucket that contains *at least* the requested block size.
     */
    typedef index_t(*indexer_function)(const std::size_t);

    /**
     @return the process-wide index function which can calculate the bucket index based on argument block size
     */
    static indexer_function indexer();
};

}
}
}

namespace memory {

/*
 A memory allocation mechanism which allows for caches (potentially 
 `thread_local`) of deallocated values for reuse on subsequent allocate() calls. 
 This allows for limiting lock contention on process-wide 
 `std::malloc()`/`std::free()`.

 This is not an allocator which can be passed to a container, as it manages 
 multiple block sizes instead of a single templated `T`. It is instead a 
 mechanism for other allocation mechanisms to build on top of.

 This cache is non-exhaustive, `std::malloc()`/`std::free()` will be called 
 internally as necessary.

 WARNING: Allocations from this mechanism cannot be directly deallocated by 
 `std::free()` or `delete`, because the allocated values contain a header 
 located at an address *before* the returned allocated pointer. Therefore any 
 allocated values from a cache must be deallocated with an instance of it (it 
 does not have to be the same instance).

 It is possible to provide object method overrides for operators `new`/`delete` 
 which utilize this mechanisms.
 */
struct cache { 
    typedef config::memory::cache::info::index_t index_t;

    struct bad_alloc : public std::exception {
        inline const char* what() const noexcept { 
            return "hce::memory::cache: cannot allocate block size of 0"; 
        }
    };

    struct bad_dealloc : public std::exception {
        inline const char* what() const noexcept { 
            return "hce::memory::cache: cannot deallocate a nullptr"; 
        }
    };

    cache() = delete;

    cache(config::memory::cache::info& info) {
        // acquire the indexer function
        indexer_ = config::memory::cache::info::indexer();

        // allocate enough memory for all buckets at once
        buckets_.reserve(info.count());

        // setup the cache buckets based on the configuration
        for(index_t i=0; i<info.count(); ++i) {
            config::memory::cache::info::bucket& b = info.at(i);
            buckets_.push_back(bucket(i, b.block, b.limit));
        }
    }

    ~cache() { }

    /// return the caller's assigned cache
    static cache& get();

    /// allocate a pointer of at least the given size
    inline void* allocate(const std::size_t size) {
        if(!size) [[unlikely]] { throw bad_alloc(); }

        const index_t index = indexer_(size);

        if(index < buckets_.size()) [[likely]] {
            return cache::from_header(buckets_[index].allocate());
        } else [[unlikely]] {
            return cache::from_header(cache::allocate_header(size, index));
        }
    }

    /// deallocate an allocate()d pointer
    inline void deallocate(const void* ptr) {
        if(!ptr) [[unlikely]] { throw bad_dealloc(); }

        // Get the header which is before the address of `ptr`. The header is 
        // the actual address which needs to be deallocated
        header* hdr = cache::to_header(ptr);
        const index_t index = hdr->index;

        if(index < buckets_.size()) [[likely]] {
            buckets_[index].deallocate(hdr);
        } else [[unlikely]] {
            cache::deallocate_header(hdr);
        }
    }

    /// return the bucket count
    inline std::size_t count() const {
        return buckets_.size();
    }

    /// return the bucket index for a given allocation size
    inline std::size_t index(std::size_t size) const {
        return indexer_(size);
    }

    /// return the count of available cached allocations for a given size
    inline std::size_t available(std::size_t size) const {
        const index_t index = indexer_(size);

        if(index < buckets_.size()) [[likely]] {
            return buckets_[index].free_count;
        } else [[unlikely]] {
            return 0;
        }
    }

    /// return the max count of available cached allocations for a given size
    inline std::size_t limit(std::size_t size) const {
        const index_t idx = indexer_(size);

        if(idx < buckets_.size()) [[likely]] {
            return buckets_[idx].limit;
        } else [[unlikely]] {
            return 0;
        }
    }
   
    /// deallocate all memory in the cache
    inline void clear() {
        for(auto& bkt : buckets_) {
            bkt.clear();
        }
    }

private:
    // memory block allocations have this before their beginning address
    struct header {
        index_t index; // index of the source bucket
    };

    // allocate a block of memory with a header
    static inline header* allocate_header(const std::size_t size, const index_t index) {
        // the final allocated value is a block containing the cache header 
        // and the requested memory block
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

        const index_t index; // the index of this bucket in the vector
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
    hce::config::memory::cache::info::indexer_function indexer_;

    // the various buckets managing allocations of different block sizes
    std::vector<bucket> buckets_;
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
 @brief deallocate an `memory::allocate()`d pointer of `size` bytes
 @param p the allocated pointer 
 @param size the size that the pointer was `memory::allocate()`d with
 */
inline void deallocate(void* p) {
    return hce::memory::cache::get().deallocate(p);
}

}
}

#endif
