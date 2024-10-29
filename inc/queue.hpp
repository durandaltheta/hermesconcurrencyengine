//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#ifndef __HERMES_COROUTINE_ENGINE_QUEUE__
#define __HERMES_COROUTINE_ENGINE_QUEUE__

#include <sstream>
#include <iterator>
#include <tuple>

#include "utility.hpp"
#include "logging.hpp"
#include "slab_allocator.hpp"

namespace hce {

/**
 @brief a singly linked queue

 This object is optimized for the needs of this project without all the features
 of a more broad-use queue. 

 This object is used in performance critical code of the coroutine scheduler, 
 design choices and optimizations are made with that in mind.

 Design Aims:
 - singly linked
 - fast iteration
 - fast append
 - fast whole-queue concatenation 
 - push on head or tail 
 - size/length tracking
 - potential allocation reuse
 - lazy allocated value construction

 Design Limitations:
 - no support for arbitrary insertion 

 This is used over `std::deque<T>` because it doesn't have fast concatenation,
 which directly impacts the processing loop of hce::scheduler.

 The allocator for this object is an `hce::slab_allocator`, limiting the 
 synchronization cost of frequent allocations/deallocations. `default_block_limit` 
 can be specified during construction to set the maximum count of elements the 
 allocator can cache.
 */
template <typename T>
struct queue : public printable {
    using value_type = T;
    static constexpr size_t default_block_limit = 
        slab_allocator<T>::default_block_limit;

    struct iterator : public std::forward_iterator_tag, public printable {
        typedef T value_type; //< iterator templated value type

        iterator() : node_(nullptr) { } 
        iterator(const iterator& rhs) : node_(rhs.node_){ } 

        static inline std::string info_name() { 
            return hce::queue<T>::info_name() + "::iterator"; 
        }

        inline std::string name() const { return iterator::info_name(); }

        /// lvalue iterator assignment
        inline const iterator& operator=(const iterator& rhs) const {
            node_ = rhs.node_;
            return *this;
        }

        /// lvalue iterator comparison
        inline bool operator==(const iterator& rhs) const {
            return node_ == rhs.node_;
        }

        /// rvalue iterator comparison
        inline bool operator==(iterator&& rhs) const { return *this == rhs; }

        /// lvalue iterator not comparison
        inline bool operator!=(const iterator& rhs) const { return !(*this == rhs); }

        /// rvalue iterator not comparison
        inline bool operator!=(iterator&& rhs) const { return *this != rhs; }

        /// retrieve reference to cached value T
        inline T& operator*() const { return node_->value; }

        /// retrieve pointer to cached value T
        inline T* operator->() const { return &(node_->value); }

        /// retrieve data from the channel iterator
        inline const iterator& operator++() const {
            node_ = node_->next;
            return *this;
        }

        /// retrieve data from the channel iterator
        inline const iterator operator++(int) const {
            node_ = node_->next;
            return *this;
        }

    private:
        iterator(typename hce::queue<T>::node* n) : node_(n) { }

        typename hce::queue<T>::node* node_;
    };

    queue(size_t slab_block_limit = default_block_limit) : 
        allocator_(slab_block_limit) { 
        HCE_MIN_CONSTRUCTOR(); 
    }

    /**
     Variant which pre-caches the pool memory.
     */
    queue(size_t slab_block_limit, hce::pre_cache pc) : 
        allocator_(slab_block_limit, pc) { 
        HCE_MIN_CONSTRUCTOR(); 
    }

    queue(const queue<T>& rhs) {
        HCE_MIN_CONSTRUCTOR(rhs);
        copy_(rhs); 
    }

    queue(queue<T>&& rhs) {
        HCE_MIN_CONSTRUCTOR(rhs);
        move_(std::move(rhs));
    }

    ~queue() { 
        HCE_MIN_DESTRUCTOR();
        node* cur = head_;

        // destruct and deallocate all nodes
        while(cur) [[likely]] { 
            node* old = cur;
            cur = cur->next;
            old->~node();
            allocator_.deallocate(old, 1); 
        } 
    }

    static inline std::string info_name() { 
        return type::templatize<T>("hce::queue"); 
    }

    inline std::string name() const { return queue<T>::info_name(); }

    inline std::string content() const {
        std::stringstream ss;
        ss << "size:" << size() << ", " << allocator_;
        return ss.str();
    }

    inline queue<T>& operator=(const queue<T>& rhs) { 
        HCE_MIN_METHOD_ENTER("operator=", rhs);
        copy_(rhs); 
        return *this;
    }

    inline queue<T>& operator=(queue<T>&& rhs) {
        HCE_MIN_METHOD_ENTER("operator=", rhs);
        move_(std::move(rhs));
        return *this;
    }

    inline iterator begin() const { return { head_ }; }
    inline iterator end() const { return {}; }

    /**
     @return the current length of the queue
     */
    inline size_t size() const { 
        HCE_TRACE_METHOD_ENTER("size");
        return size_; 
    }

    /**
     @return true if empty, else false
     */
    inline bool empty() const { 
        HCE_TRACE_METHOD_ENTER("empty");
        return !size_; 
    }

    /**
     @return a reference to the front of the queue
     */
    inline T& front() { 
        HCE_TRACE_METHOD_ENTER("front");
        return head_->value; 
    }

    /**
     @brief emplace an element on the back of the queue 
     */
    template <typename... As>
    inline void emplace_back(As&&... as) {
        HCE_MIN_METHOD_ENTER("emplace_back");
        node* next = allocator_.allocate(1);

        // placement new construction
        new(next) node(std::forward<As>(as)...);

        if(size_) [[likely]] {
            tail_->next = next;
            tail_ = next;
        } else [[unlikely]] {
            head_ = next;
            tail_ = next;
        } 
            
        ++size_;
    }

    /**
     @brief emplace an element on the front of the queue 
     */
    template <typename... As>
    inline void emplace_front(As&&... as) {
        HCE_MIN_METHOD_ENTER("emplace_front");
        node* next = allocator_.allocate(1);

        // placement new construction
        new(next) node(std::forward<As>(as)...);

        if(size_) [[likely]] {
            node* old = head_;
            head_ = next;
            head_->next = old;
        } else [[unlikely]] {
            head_ = next;
            tail_ = next;
        } 
            
        ++size_;
    }
    
    /**
     @brief lvalue push an element on the back of the queue 
     */
    inline void push_back(const T& t) { 
        HCE_MIN_METHOD_ENTER("push_back");
        emplace_front(t); 
    }
    
    /**
     @brief rvalue push an element on the back of the queue 
     */
    inline void push_back(T&& t) { 
        HCE_MIN_METHOD_ENTER("push_back");
        emplace_back(std::move(t)); 
    }
    
    /**
     @brief lvalue push an element on the front of the queue 
     */
    inline void push_front(const T& t) { 
        HCE_MIN_METHOD_ENTER("push_front");
        emplace_front(t); 
    }
    
    /**
     @brief rvalue push an element on the front of the queue 
     */
    inline void push_front(T&& t) { 
        HCE_MIN_METHOD_ENTER("push_front");
        emplace_back(std::move(t)); 
    }

    /**
     @brief pop off the front of the queue
     */
    inline void pop() {
        HCE_MIN_METHOD_ENTER("pop");
        node* old = head_;
        head_ = head_->next;
        old->~node();
        allocator_.deallocate(old, 1);
        --size_;
    }
    
    /**
     @brief iterate the list front to back and find the first matching t

     The returned first element in the tuple will == end() if no matching t was 
     found. 

     @param t a comparison value
     @return a tuple of the iterator to the found element, if any, and the element previous to it
     */
    inline std::tuple<iterator,iterator> find(const T& t) {
        HCE_MIN_METHOD_ENTER("find");
        auto prev = head_;
        auto cur = prev;

        while(cur && cur->value != t) {
            prev = cur;
            cur = cur->next;
        }

        return { { cur }, { prev } };
    }

    /**
     @brief iterate the list front to back and erase the first matching t
     @param target the iterator to element to erase 
     @param prev the iterator to the element preceding the target
     */
    inline void erase(iterator target, iterator prev) {
        HCE_MIN_METHOD_ENTER("erase");
        prev->next = target->next;
        target->~node();
    }

    /**
     @brief steal the elements of the argument queue and concatenate them to the end 

     This is not tied to the RAII lifecycle of either object. The argument queue 
     will still be valid after this call.

     @param rhs queue to concatenate
     */
    inline void concatenate(queue<T>& rhs) {
        HCE_MIN_METHOD_ENTER("concatenate");
        if(rhs.size_) {
            if(size_) { // our head_ & tail_ are initialized
                tail_->next = rhs.head_;
                tail_ = rhs.tail_;
                size_ += rhs.size_;
                rhs.head_ = nullptr;
                rhs.tail_ = nullptr;
                rhs.size_ = 0;
            } else { // lhs is empty, swap values
                std::swap(head_, rhs.head_);
                std::swap(tail_, rhs.tail_);
                std::swap(size_, rhs.size_);
            }
        } // else nothing to do
    }

private:
    // element in the queue
    struct node {
        template <typename... As>
        node(As&&... as) : value(std::forward<As>(as)...), next(nullptr) { }

        T value;
        node* next;
    };

    // deep copy the argument queue's elements
    inline void copy_(const queue<T>& rhs) {
        node* rhs_head = rhs.head_;

        while(rhs_head) [[likely]] {
            push(rhs_head->value); // deep copy the value
            rhs_head = rhs_head->next;
        }

        // ignore the allocator
    }

    // move swap members
    inline void move_(queue<T>&& rhs) {
        std::swap(head_, rhs.head_);
        std::swap(tail_, rhs.tail_);
        std::swap(size_, rhs.size_);
        std::swap(allocator_, rhs.allocator_);
    }

    node* head_ = nullptr; // head of list
    node* tail_ = nullptr; // tail of list
    size_t size_ = 0; // length of list 
    hce::slab_allocator<node> allocator_; // memory allocator
};

}

#endif
