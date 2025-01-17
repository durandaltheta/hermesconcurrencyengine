//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#ifndef HERMES_COROUTINE_ENGINE_MEMORY
#define HERMES_COROUTINE_ENGINE_MEMORY

#include <cstdlib>
#include <limits>
#include <memory>

#include "utility.hpp"

namespace tlmem {
namespace config {
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
            scheduler, //< a thread designated for an hce::scheduler
            global //< the thread designated for the global hce::scheduler
        };

        /// get the calling thread's type
        static type& get_type();
    };

    struct bucket {
        const std::size_t block; //< bucket element block size
        const std::size_t limit; //< maximum element count for the bucket
    };

    // the system thread byte limit
    static std::size_t system_byte_limit();

    // the global scheduler byte limit
    static std::size_t global_byte_limit();

    // the default scheduler byte limit
    static std::size_t scheduler_byte_limit();

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
    typedef std::size_t(*index_function)(std::size_t);

    /**
     @return the index function which can calculate the index based on argument block size
     */
    virtual index_function indexer() = 0;
};

}
}

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
            return "tlmem::cache: cannot allocate block size of 0"; 
        }
    };

    struct bad_dealloc : public std::exception {
        inline const char* what() const noexcept { 
            return "tlmem::cache: cannot deallocate a nullptr"; 
        }
    };

    struct bad_size_dealloc : public std::exception {
        inline const char* what() const noexcept { 
            return "tlmem::cache: cannot allocate block size of 0"; 
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
            buckets_.push_back(bucket(i, b.size, b.limit));
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
            size(s), 
            limit(l) 
        { }

        ~bucket() { clear(); }

        // allocate a chunk of memory from the bucket
        inline header* allocate() {
            if (free_list.empty()) {
                return cache::allocate_header(size, index);
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
        const std::size_t size; // the block memory size
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
};

/**
 @brief allocate a pointer of `size` bytes
 @param size the size of the requested allocation
 @return an allocated pointer of at least `size` bytes
 */
inline void* allocate(std::size_t size) {
    return tlmem::cache::get().allocate(size);
}

/**
 @brief deallocate a pointer of `size` bytes
 @param p the allocated pointer 
 @param size the size that the pointer was `memory::allocate()` with
 */
inline void deallocate(void* p) {
    return tlmem::cache::get().deallocate(p);
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

#endif
