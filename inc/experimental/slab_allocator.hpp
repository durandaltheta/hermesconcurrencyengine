//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#ifndef __HERMES_COROUTINE_ENGINE_SLAB_ALLOCATOR__
#define __HERMES_COROUTINE_ENGINE_SLAB_ALLOCATOR__ 

#include <cstdlib>
#include <type_traits>
#include <string>
#include <sstream>
#include <algorithm>
#include <vector>

#include "utility.hpp"
#include "logging.hpp"
#include "memory.hpp"

#ifndef HCESLABALLOCATORDEFAULTBLOCKLIMIT
#define HCESLABALLOCATORDEFAULTBLOCKLIMIT 64
#endif

namespace hce {

/**
 @brief a slab allocator of one or more blocks of contiguous memory 

 This object is designed for speed and lazy cached slab growth. Its purpose is 
 to make frequent allocation/deallocation of a given type T more CPU efficient.

 Slab allocators have more limitations than pool allocators. However, because a 
 pre-sized slab allocator (to its block limit) is a chunk of continuous memory,
 this makes it more ideal for allocating hashable keys, because a unique memory 
 address can be used to avoid hash collisions.

 Design Aims:
 - lazy slab growth
 - optional pre-caching of slabs
 - exponential amortized allocated slab growth (like std::vector)
 - constant time allocation/deallocation when re-using allocated pointers of `T`
 - no exception handling (for speed)
 - usable as an std:: container allocator

 Design Limitations:
 - slabs can only grow, never shrink
 - sub-optimal memory efficiency (block size guaranteed larger than sizeof(T))
 - array allocation/deallocation of `T` uses process wide mechanism
 - each block requires one-time footer construction
 - memory *MUST* be deallocated by the same slab_allocator that allocated it
 */
template <typename T>
struct slab_allocator : public printable {
    using value_type = T;

    /// the default block limit
    static constexpr size_t default_block_limit = HCESLABALLOCATORDEFAULTBLOCKLIMIT;

    /// necessary for rebinding this allocator to different types
    template <typename U>
    struct rebind {
        using other = slab_allocator<U>;
    };

    /**
     @brief construct a slab 
     @param block_limit the maximum count of blocks this allocator can grow to
     */
    slab_allocator(size_t block_limit = default_block_limit) :
        block_limit_(block_limit) 
    { 
        HCE_MIN_CONSTRUCTOR(block_limit);
    }

    /**
     Immediately grow the slab allocator to *at least* the requested pre-cache 
     size.

     Passing in a `hce::pre_cache` with its `size` equal to `block_limit` will 
     result in the allocator permanently having a single slab of contiguous 
     memory. As a technical matter this can be useful in 
     */
    slab_allocator(size_t block_limit, hce::pre_cache pc) : 
        block_limit_(block_limit) 
    { 
        HCE_MIN_CONSTRUCTOR(block_limit, std::string("hce::pre_cache::size:") + pc.size);

        if(pc.size) {
            grow_(pc.size);
        }
    }

    /**
     Allocated bytes are never copied. This operation sets the block growth 
     limit, allowing this object to be used as a templated std:: Allocator which 
     uses copy construction.
     */
    slab_allocator(const slab_allocator<T>& rhs) : 
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
    slab_allocator(const slab_allocator<U>& rhs) : block_limit_(rhs.limit()) {
        // Copy the block limit during rebind
        HCE_MIN_CONSTRUCTOR(std::string("const ") + rhs.to_string() + "&");
    }

    /**
     move construct a slab allocator
     */
    slab_allocator(slab_allocator<T>&& rhs) {
        HCE_MIN_CONSTRUCTOR(rhs.to_string() + "&&");
        swap_(std::move(rhs));
    }

    ~slab_allocator() { 
        HCE_MIN_DESTRUCTOR(); 

        for(auto slab : slabs_) {
            hce::deallocate(slab);
        }
    }

    static inline std::string info_name() { 
        return type::templatize<T>("hce::slab_allocator"); 
    }

    inline std::string name() const { return slab_allocator<T>::info_name(); }

    inline std::string content() const {
        std::stringstream ss;
        ss << "limit:" << limit() << ", size:" << size() << ", used:" << used();
        return ss.str();
    }

    slab_allocator<T>& operator=(const slab_allocator<T>& rhs) { 
        HCE_MIN_METHOD_ENTER("operator=", std::string("const ") + rhs.to_string() + "&");
        block_limit_ = rhs.block_limit_;
        // allocated bytes are never copied
        return *this;
    }

    slab_allocator<T>& operator=(slab_allocator<T>&& rhs) {
        HCE_MIN_METHOD_ENTER("operator=", rhs.to_string() + "&&");
        swap_(std::move(rhs));
        return *this;
    }
   
    /// allocate a block of memory the size of T * n
    inline T* allocate(size_t n) {
        HCE_MIN_METHOD_ENTER("allocate", n);
        T* t;

        if(n==1 && (available() || grow_())) [[likely]] { 
            // prefer to retrieve memory from the slab
            t = pop_free_();
        } else [[unlikely]] { 
            // allocate memory if necessary
            block* b = hce::allocate<block>(n);
            reinterpret_cast<block*>(b)->from_slab = false;
            t = reinterpret_cast<T*>(b);
        }

        return t;
    }

    /**
     @brief deallocate a block of memory 

     It is an error for the user to pass a pointer to this function that was 
     not acquired from allocate() by the same slab_allocator
     */
    inline void deallocate(T* t, size_t n) {
        HCE_MIN_METHOD_ENTER("deallocate", t, n);
        
        if(reinterpret_cast<block*>(t)->from_slab) [[likely]] {
            // memory is from a slab
            push_free_(t); 
        } else [[unlikely]] {
            // free memory if necessary
            hce::deallocate(t); 
        }
    }
    
    /// return the allocator's block count limit
    inline size_t limit() const { 
        HCE_TRACE_METHOD_ENTER("limit",block_limit_);
        return block_limit_; 
    }

    /// return the allocator's current block count
    inline size_t size() const { 
        HCE_TRACE_METHOD_ENTER("size",block_total_count_);
        return block_total_count_; 
    }

    /// return the slab's used (allocated) block count
    inline size_t used() const { return block_total_count_ - block_available_count_; }

    /// return the slab's available (unallocated) block count
    inline size_t available() const { return block_available_count_; }

private:
    // the actual allocated block of memory in a slab
    struct block {
        using storage_type = 
            typename std::aligned_storage<sizeof(T) >= sizeof(void*) 
                ? sizeof(T) 
                : sizeof(void*), alignof(T)>::type;

        // because this is the first element, it can be acquired by casting a 
        // block at an arbitrary index to T*. Use std::aligned_storage to ensure 
        // value is at least as large and aligned as a pointer
        storage_type value;

        // true if part of an allocated slab, else false
        bool from_slab;
    };

    /*
     Slab memory is arbitrarily configured as nodes when not allocated (not 
     in user control). This object is easier to understand than casting each 
     index to void* when iterating the free list.
     */
    struct node {
        node* next;
    };

    /*
     Add a new slab of pre-allocated memory if we have room to grow.

     Return true if a new slab was added, else false
     */
    inline bool grow_(size_t requested_block_count=0) {
        if(block_total_count_ < block_limit_) {
            // determine the block count of the new slab
            size_t block_count = std::min(
                block_limit_ - block_total_count_, // can never allocate above the limit
                std::max(
                    requested_block_count, // potentially large requested allocation
                    block_total_count_ ? block_total_count_ * 2 : 1)); // amortized growth attempt 
            HCE_TRACE_METHOD_BODY("grow_","block_count:",block_count);
           
            // allocate the new slab with the calculated block count
            void* slab = hce::allocate<block>(block_count);
            
            HCE_TRACE_METHOD_BODY("grow_","slab:",slab);

            // store the slab pointer
            slabs_.push_back(slab);

            // update the block total and available counts
            block_total_count_ += block_count; 
            block_available_count_ += block_count;

            // get the first node in the slab (which is the tail of the new list)
            node* current = reinterpret_cast<node*>(slab);

            // concatenate the old list to the tail of the new list
            current->next = free_head_;

            // set the source index and slab pointer on the first element
            reinterpret_cast<block*>(current)->from_slab = true;

            // decrement the count after handling the first element
            --block_count;

            // setup a forward list of remaining nodes in the slab
            while(block_count) [[likely]] {
                node* next = reinterpret_cast<node*>(
                    reinterpret_cast<std::byte*>(current) + block_size_);
                next->next = current;
                current = next;

                // set the source index and slab pointer
                reinterpret_cast<block*>(current)->from_slab = true;
                --block_count;
            }

            free_head_ = current; // update the head
            return true;
        } else {
            return false;
        }
    }

    // pop from the front of the list
    inline T* pop_free_() {
        node* next = free_head_->next;
        T* ret = reinterpret_cast<T*>(free_head_);
        free_head_ = next;
        --block_available_count_;
        return ret;
    }

    // push on the front of the list
    inline void push_free_(T* t) {
        node* next = reinterpret_cast<node*>(t);
        next->next = free_head_;
        free_head_ = next;
        ++block_available_count_;
    }

    inline void swap_(slab_allocator<T>&& rhs) {
        std::swap(block_limit_, block_limit_); 
        std::swap(block_total_count_, rhs.block_total_count_);
        std::swap(block_available_count_, rhs.block_available_count_);
        std::swap(free_head_, rhs.free_head_);
        std::swap(slabs_, rhs.slabs_);
    }

    static constexpr size_t block_size_ = sizeof(block); // element byte size
    size_t block_limit_ = 0; // maximum possible count of elements
    size_t block_total_count_ = 0; // current allocated count of elements
    size_t block_available_count_ = 0; // currently used count of elements
    node* free_head_ = nullptr; // front of the free elements list 
    std::vector<void*> slabs_; // allocated memory management 
};

}

// Comparison operator for slab allocator
template <typename T, typename U>
bool operator==(const hce::slab_allocator<T>&, const hce::slab_allocator<U>&) {
    return true;
}

template <typename T, typename U>
bool operator!=(const hce::slab_allocator<T>&, const hce::slab_allocator<U>&) {
    return false;
}

#endif
