#pragma once
//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 

#include <sstream>
#include <iterator>
#include <tuple>

#include "hce/utility.hpp"
#include "hce/logging.hpp"
#include "hce/alloc.hpp"

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
 - singly linked (not a block of contiguous memory)
 - no support for sorting capabilities
 - arbitrary erasure is O(n) (singly linked); intended for non-hot paths
   such as multiplex cleanup

 This is preferred by this project over `std::deque<T>` because that object 
 doesn't have fast concatenation which directly impacts the processing loop of 
 hce::scheduler.

 The default allocator for this object is an `hce::pool_allocator`, limiting the 
 synchronization cost of frequent allocations/deallocations. 
 */
template <typename T, typename Allocator = hce::pool_allocator<T>>
struct list : public printable {
    using value_type = T;

    // element in the list
    struct node : public printable {
        template <typename... As>
        node(As&&... as) : value(std::forward<As>(as)...), next(nullptr) { }

        virtual ~node() { }

        static inline std::string info_name() { 
            return hce::list<T>::info_name() + "::node"; 
        }

        inline std::string name() const { return node::info_name(); }

        T value;
        node* next;
    };


    // forward iterator (node* under the hood)
    struct iterator {
        using iterator_category = std::forward_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using pointer = T*;
        using reference = T&;

        iterator() = default;

        reference operator*() const { return n_->value; }
        pointer operator->() const { return &n_->value; }

        iterator& operator++() {
            n_ = n_->next;
            return *this;
        }

        iterator operator++(int) {
            auto tmp = *this;
            ++*this;
            return tmp;
        }

        bool operator==(const iterator& o) const { return n_ == o.n_; }
        bool operator!=(const iterator& o) const { return n_ != o.n_; }

    private:
        node* n_ = nullptr;
        friend class list;
        explicit iterator(node* n) : n_(n) {}
    };

    // values are typically pointers; no deep const needed
    using const_iterator = iterator; 

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

    virtual ~list() { 
        HCE_MIN_DESTRUCTOR();
        node* cur = head_;

        // destruct and deallocate all nodes
        while(cur) [[likely]] { 
            node* old = cur;
            cur = cur->next;
            old->~node();
            allocator_.deallocate(old,1); 
        } 
    }

    static inline std::string info_name() { 
        return type::templatize<T>("hce::list"); 
    }

    inline std::string name() const { return list<T>::info_name(); }

    inline std::string content() const {
        std::stringstream ss;
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
        allocator_.deallocate(old,1);
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

    iterator begin() { return iterator(head_); }
    iterator end() { return iterator(nullptr); }
    const_iterator begin() const { return iterator(head_); }
    const_iterator end() const { return iterator(nullptr); }
    const_iterator cbegin() const { return begin(); }
    const_iterator cend() const { return end(); }

    /**
     @brief erase the element at pos (O(n) for singly-linked)
     @return iterator to the following element (or end)
     */
    iterator erase(iterator pos) {
        HCE_MIN_METHOD_ENTER("erase");
        if(!pos.n_) { return pos; }

        node* prev = nullptr;
        node* cur = head_;

        while(cur && cur != pos.n_) {
            prev = cur;
            cur = cur->next;
        }

        if(!cur) { return end(); }

        node* next = cur->next;
        if(prev) { prev->next = next; }
        else head_ = next;
        if(cur == tail_) { tail_ = prev; }

        cur->~node();
        allocator_.deallocate(cur, 1);
        --size_;

        return iterator(next);
    }

    /**
     @brief remove all elements for which pred returns true (single O(n) pass)
     @return number of elements removed
     */
    template <typename Pred>
    size_t remove_if(Pred pred) {
        HCE_MIN_METHOD_ENTER("remove_if");
        size_t removed = 0;
        node* prev = nullptr;
        node* cur = head_;

        while(cur) {
            if(pred(cur->value)) {
                node* next = cur->next;
                if(prev) { prev->next = next; }
                else head_ = next;
                if(cur == tail_) { tail_ = prev; }

                cur->~node();
                allocator_.deallocate(cur, 1);
                --size_;
                ++removed;
                cur = next;
            } else {
                prev = cur;
                cur = cur->next;
            }
        }

        return removed;
    }

    /**
     @brief erase the first element equal to value
     @return true if an element was erased
     */
    bool erase(const T& value) {
        HCE_MIN_METHOD_ENTER("erase(value)");
        return remove_if([&](const T& v) { return v == value; }) > 0;
    }

    /**
     @brief find the first element for which pred returns true
     @return iterator to the element, or end()
     */
    template <typename Pred>
    iterator find_if(Pred pred) {
        for (auto it = begin(); it != end(); ++it) {
            if(pred(*it)) { return it; }
        }
        
        return end();
    }

private:
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
    typename Allocator::template rebind<node>::other allocator_; // memory allocator
};

}
