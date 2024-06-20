//SPDX-License-Identifier: Apache-2.0
//Author: Blayne Dennis 
#ifndef __HERMES_COROUTINE_ENGINE_CIRCULAR_BUFFER__
#define __HERMES_COROUTINE_ENGINE_CIRCULAR_BUFFER__

// c++
#include <vector> 
#include <exception> 

// local
#include "utility.hpp"

namespace hce {

/**
 @brief a simple circular_buffer implementation
 */
template <typename T>
struct circular_buffer : public printable {
    struct push_on_full : public std::exception {
        const char* what() const noexcept { 
            const char* s = "cannot call push() when the buffer is full";
            HCE_ERROR_LOG(s);
            return s; 
        }
    };

    struct front_on_empty : public std::exception {
        const char* what() const noexcept { 
            const char* s = "cannot call front() when the buffer is empty"; 
            HCE_ERROR_LOG(s);
            return s;
        }
    };

    struct pop_on_empty : public std::exception {
        const char* what() const noexcept { 
            const char* s = "cannot call pop() when the buffer is empty"; 
            HCE_ERROR_LOG(s);
            return s;
        }
    };

    circular_buffer(size_t sz = 1) : 
        buffer_(sz ? sz : 1), // enforce a minimum buffer size of 1
        size_(0),
        back_idx_(0),
        front_idx_(0)
    { 
        HCE_MIN_CONSTRUCTOR();
    }

    circular_buffer(const circular_buffer<T>&) = default;
    circular_buffer(circular_buffer<T>&&) = default;

    ~circular_buffer() { HCE_MIN_DESTRUCTOR(); }
    
    circular_buffer& operator=(const circular_buffer<T>&) = default;
    circular_buffer& operator=(circular_buffer<T>&&) = default;

    inline const char* nspace() const { return "hce"; }
    inline const char* name() const { return "circular_buffer"; }

    /// return the maximum size of the buffer 
    inline size_t capacity() { 
        HCE_MIN_METHOD_ENTER("capacity");
        return buffer_.size(); 
    }

    /// return the current size of the buffer
    inline size_t size() { 
        HCE_MIN_METHOD_ENTER("size");
        return size_; 
    }

    /// return the available slots in the buffer
    inline size_t remaining() { 
        HCE_MIN_METHOD_ENTER("remaining");
        return capacity() - size(); 
    }

    /// return true if the buffer is empty, else false
    inline bool empty() { 
        HCE_MIN_METHOD_ENTER("empty");
        return 0 == size(); 
    }
    
    /// return true if the buffer is full, else false
    inline bool full() { 
        HCE_MIN_METHOD_ENTER("full");
        return capacity() == size(); 
    }

    /// return a reference to the element at the front of the buffer
    inline T& front() { 
        HCE_MIN_METHOD_ENTER("front");
        if(empty()) { throw front_on_empty(); }
        return buffer_[front_idx_]; 
    }

    /// lvalue push an element on the back of the buffer
    inline void push(const T& t) {
        HCE_MIN_METHOD_ENTER("push");
        if(!full()) {
            buffer_[back_idx_] = t;
            increment(back_idx_);
            ++size_;
        } else { throw push_on_full(); }
    }

    /// rvalue push an element on the back of the buffer
    inline void push(T&& t) {
        HCE_MIN_METHOD_ENTER("push");
        if(!full()) {
            buffer_[back_idx_] = std::move(t);
            increment(back_idx_);
            ++size_;
        } else { throw push_on_full(); }
    }

    /// pop the front element off the buffer
    inline void pop() {
        HCE_MIN_METHOD_ENTER("pop");
        if(!empty()) {
            increment(front_idx_);
            --size_;
        } else { throw pop_on_empty(); }
    }

private:
    inline void increment(size_t& idx) { 
        size_t test_idx = idx + 1;
        idx = test_idx < capacity() ? test_idx : 0; 
    }

    std::vector<T> buffer_; // the buffer
    size_t size_; // count of used buffer indexes
    size_t back_idx_; // index of the back of the buffer
    size_t front_idx_; // index of the front of the buffer
};

}

#endif
