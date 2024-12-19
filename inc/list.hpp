//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#ifndef __HERMES_COROUTINE_ENGINE_list__
#define __HERMES_COROUTINE_ENGINE_list__

#include <sstream>
#include <iterator>
#include <tuple>

#include "utility.hpp"
#include "logging.hpp"
#include "pool_allocator.hpp"

namespace hce {

/**
 @brief a singly linked list

 This object is optimized for the needs of this project without all the features
 of a more broad-use list. 

 This object is used in performance critical code of the coroutine scheduler, 
 design choices and optimizations are made with that in mind.

 Design Aims:
 - singly linked (faster manipulation than doubly linked)
 - constant time iteration
 - constant time append
 - constant time whole-list concatenation 
 - push on head or tail (LIFO or FIFO)
 - size/length tracking
 - defaults to pool_allocator<T> for efficient memory reuse
 - lazy allocated value construction

 Design Limitations:
 - can only iterate from front to back (singly linked)
 - can only read and pop from head (singly linked)
 - no iterator support
 - no support for arbitrary insertion or erasure
 - no support for sorting capabilities

 This is preferred by this project over `std::deque<T>` because it doesn't have 
 fast concatenation which directly impacts the processing loop of 
 hce::scheduler.

 The allocator for this object is an `hce::pool_allocator`, limiting the 
 synchronization cost of frequent allocations/deallocations. `block_limit` can 
 be specified during construction to set the maximum count of elements the 
 allocator can cache for reuse.
 */
template <typename T, typename Allocator = hce::pool_allocator<T>>
struct list : public printable {
    using value_type = T;

    list() { HCE_MIN_CONSTRUCTOR(); }

    list(const Allocator& allocator) : 
        allocator_(allocator) { 
        HCE_MIN_CONSTRUCTOR(allocator); 
    }

    list(const list<T>& rhs) {
        HCE_MIN_CONSTRUCTOR(rhs);
        copy_(rhs); 
    }

    list(list<T>&& rhs) {
        HCE_MIN_CONSTRUCTOR(rhs);
        move_(std::move(rhs));
    }

    ~list() { 
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

    static inline hce::string info_name() { 
        return type::templatize<T>("hce::list"); 
    }

    inline hce::string name() const { return list<T>::info_name(); }

    inline hce::string content() const {
        hce::stringstream ss;
        ss << "size:" << size_ << ", " << allocator_;
        return ss.str();
    }

    inline list<T>& operator=(const list<T>& rhs) { 
        HCE_MIN_METHOD_ENTER("operator=", rhs);
        copy_(rhs); 
        return *this;
    }

    inline list<T>& operator=(list<T>&& rhs) {
        HCE_MIN_METHOD_ENTER("operator=", rhs);
        move_(std::move(rhs));
        return *this;
    }

    /**
     @return the current length of the list
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
     @return a reference to the front of the list
     */
    inline T& front() { 
        HCE_TRACE_METHOD_ENTER("front");
        return head_->value; 
    }

    /**
     @brief emplace an element on the back of the list 
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
     @brief emplace an element on the front of the list 
     */
    template <typename... As>
    inline void emplace_front(As&&... as) {
        HCE_MIN_METHOD_ENTER("emplace_front");
        node* next = allocator_.allocate(1);

        // placement new construction
        new(next) node(std::forward<As>(as)...);

        if(size_) [[likely]] {
            next->next = head_;
            head_ = next;
        } else [[unlikely]] {
            head_ = next;
            tail_ = next;
        } 
            
        ++size_;
    }
    
    /**
     @brief lvalue push an element on the back of the list 
     */
    inline void push_back(const T& t) { 
        HCE_MIN_METHOD_ENTER("push_back");
        emplace_back(t); 
    }
    
    /**
     @brief rvalue push an element on the back of the list 
     */
    inline void push_back(T&& t) { 
        HCE_MIN_METHOD_ENTER("push_back");
        emplace_back(std::move(t)); 
    }
    
    /**
     @brief lvalue push an element on the front of the list 
     */
    inline void push_front(const T& t) { 
        HCE_MIN_METHOD_ENTER("push_front");
        emplace_front(t); 
    }
    
    /**
     @brief rvalue push an element on the front of the list 
     */
    inline void push_front(T&& t) { 
        HCE_MIN_METHOD_ENTER("push_front");
        emplace_front(std::move(t)); 
    }

    /**
     @brief pop off the front of the list
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
     @brief steal the elements of the argument list and concatenate them to the end 

     This is not tied to the RAII lifecycle of either object. The argument list 
     will still be valid after this call.

     @param rhs list to concatenate
     */
    inline void concatenate(list<T>& rhs) {
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
                // do NOT swap allocator
            }
        } // else nothing to do
    }

private:
    // element in the list
    struct node : public printable {
        template <typename... As>
        node(As&&... as) : value(std::forward<As>(as)...), next(nullptr) { }

        static inline hce::string info_name() { 
            return hce::list<T>::info_name() + "::node"; 
        }

        inline hce::string name() const { return node::info_name(); }

        T value;
        node* next;
    };

    // deep copy the argument list's elements
    inline void copy_(const list<T>& rhs) {
        node* rhs_head = rhs.head_;

        while(rhs_head) [[likely]] {
            push_back(rhs_head->value); // deep copy the value
            rhs_head = rhs_head->next;
        }

        // ignore the allocator
    }

    // move swap members
    inline void move_(list<T>&& rhs) {
        std::swap(head_, rhs.head_);
        std::swap(tail_, rhs.tail_);
        std::swap(size_, rhs.size_);
        std::swap(allocator_, rhs.allocator_);
    }

    node* head_ = nullptr; // head of list
    node* tail_ = nullptr; // tail of list
    size_t size_ = 0; // length of list 
    typename Allocator::rebind<node>::other allocator_; // memory allocator
};

}

#endif
