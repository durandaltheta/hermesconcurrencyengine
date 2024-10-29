//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#ifndef __HERMES_COROUTINE_ENGINE_SLAB_ALLOCATOR__
#define __HERMES_COROUTINE_ENGINE_SLAB_ALLOCATOR__ 

#include <cstdlib>
#include <string>
#include <sstream>
#include <algorithm>
#include <unordered_set>

#include "utility.hpp"
#include "logging.hpp"
#include "memory.hpp"

#ifndef HCESLABALLOCATORDEFAULTBLOCKLIMIT
#define HCESLABALLOCATORDEFAULTBLOCKLIMIT 64
#endif

namespace hce {

/**
 @brief a slab allocator of one or more blocks of contiguous memory 

 This object is designed for speed and lazy cached slab growth. 

 Design Aims:
 - lazy slab growth
 - optional pre-caching of slabs
 - exponential amortized allocation cache growth (like std::vector)
 - constant time allocation/deallocation when re-using allocated pointers of `T`
 - no exception handling (for speed)

 Design Limitations:
 - can only grow, never shrink
 - sub-optimal memory efficiency (block size guaranteed larger than sizeof(T))
 - array allocation/deallocation of `T` uses process wide mechanism
 - each block requires one-time footer construction
 */
template <typename T>
struct slab_allocator : public printable {
    using value_type = T;
    static constexpr size_t default_block_limit = 
        HCESLABALLOCATORDEFAULTBLOCKLIMIT;

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
        HCE_MIN_CONSTRUCTOR(rhs, std::string("hce::pre_cache::size:") + pc.size);
        grow_(pc.size);
    }

    /**
     Allocated bytes are never copied. This operation sets the block grow limit,
     allowing this object to be used as a templated std:: Allocator which uses
     copy construction.
     */
    slab_allocator(const slab_allocator<T>& rhs) : 
        block_limit_(rhs.block_limit_) 
    { 
        HCE_MIN_CONSTRUCTOR(std::string("const ") + rhs.to_string() + "&");
    }

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

    /// all slab_allocator<T>s use the global alloc()/free()
    inline bool operator==(const slab_allocator<T>&) { return true; }
   
    /// allocate a block of memory the size of n*T
    inline T* allocate(size_t n) {
        HCE_MIN_METHOD_ENTER("allocate", n);
        T* t;

        if(n==1 && (remaining() || grow_())) [[likely]] { 
            // prefer to retrieve from the slab
            t = pop_free_();
        } else [[unlikely]] { 
            // allocate memory if necessary
            block* b = hce::allocate<block>(n);
            reinterpret_cast<block*>(b)->slab = nullptr;
            t = reinterpret_cast<T*>(t);
        }

        return t;
    }

    /// deallocate a block of memory
    inline void deallocate(T* t, size_t n) {
        HCE_MIN_METHOD_ENTER("deallocate", t, n);
        auto it = slabs_.find(reinterpret_cast<block*>(t)->slab);

        if(it == slabs_.end()) [[unlikely]] {
            // free memory if necessary
            hce::deallocate(t); 
        } else [[likely]] {
            // memory is from a slab
            push_free_(t); 
        }
    }
    
    /// return the allocator's block count limit
    inline size_t limit() const { 
        HCE_TRACE_METHOD_ENTER("limit",block_limit_);
        return block_limit_; 
    }

    /// return the allocator's current block count
    inline size_t size() const { 
        HCE_TRACE_METHOD_ENTER("size",block_count_);
        return block_count_; 
    }

    /// return the slab's used block count
    inline size_t used() const { return blocks_used_; }

    /// return the slab's remaining() count
    inline size_t remaining() const { return block_count_ - blocks_used_; }

private:
    // the actual allocated block of memory in a slab
    struct block {
        // because this is the first element, it can be acquired by casting a 
        // block at an arbitrary index to T*
        T value; 
        
        // pointer to the source slab
        void* slab; 
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
        if(block_count_ < block_limit_) {
            // determine the block count of the new slab
            size_t block_count = std::min(
                block_limit_ - block_count_, // can never allocate above the limit
                std::max(
                    requested_block_count, // potentially large requested allocation
                    block_count_ ? block_count_ * 2 : 1)); // amortized growth attempt
           
            // allocate the new slab
            void* slab = hce::allocate<block>(block_count);

            // get the first node in the slab (which is the tail)
            node* current = reinterpret_cast<node*>(slab);

            // concatenate the old list to the tail of the new list
            current->next = free_head_;

            // store the slab pointer
            slabs_.insert(slab);

            // update the block count
            block_count_ += block_count; 

            // set the source slab pointer
            reinterpret_cast<block*>(current)->slab = slab;

            // decrement the count after handling the first element
            --block_count;

            // setup a forward list of remaining nodes in the slab
            while(block_count) {
                node* next = current + block_size_;
                next->next = current;
                current = next;

                // set the source slab pointer
                reinterpret_cast<block*>(current)->slab = slab;
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
        ++blocks_used_;
        return ret;
    }

    // push on the front of the list
    inline void push_free_(T* t) {
        node* next = reinterpret_cast<node*>(t);
        next->next = free_head_;
        free_head_ = next;
        --blocks_used_;
    }

    inline void swap_(slab_allocator<T>&& rhs) {
        std::swap(block_limit_, block_limit_); 
        std::swap(block_count_, rhs.block_count_);
        std::swap(blocks_used_, rhs.blocks_used_);
        std::swap(free_head_, rhs.free_head_);
        std::swap(slabs_, rhs.slabs_);
    }

    static constexpr size_t block_size_ = alignof(block); // element byte size
    size_t block_limit_ = 0; // maximum possible count of elements
    size_t block_count_ = 0; // current allocated count of elements
    size_t blocks_used_ = 0; // currently used count of elements
    node* free_head_ = nullptr; // front of the free elements list 
    std::unordered_set<void*> slabs_; // allocated memory management
};

}

#endif
