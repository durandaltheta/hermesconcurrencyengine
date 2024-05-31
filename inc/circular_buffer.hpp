//SPDX-License-Identifier: Apache-2.0
//Author: Blayne Dennis 
#ifndef __HERMES_COROUTINE_ENGINE_CIRCULAR_BUFFER__
#define __HERMES_COROUTINE_ENGINE_CIRCULAR_BUFFER__

// c++
#include <vector> 
#include <exception> 

// local
#include "loguru.hpp"

namespace hce {

/**
 @brief a simple circular_buffer implementation
 */
template <typename T>
struct circular_buffer {
    struct push_on_full : public std::exception {
        const char* what() const noexcept { 
            const char* s = "cannot call push() when the buffer is full";
            LOG_F(ERROR, s);
            return s; 
        }
    };

    struct front_on_empty : public std::exception {
        const char* what() const noexcept { 
            const char* s = "cannot call front() when the buffer is empty"; 
            LOG_F(ERROR, s);
            return s;
        }
    };

    struct pop_on_empty : public std::exception {
        const char* what() const noexcept { 
            const char* s = "cannot call pop() when the buffer is empty"; 
            LOG_F(ERROR, s);
            return s;
        }
    };

    circular_buffer() = delete;

    circular_buffer(size_t sz) : 
        capacity_(sz ? sz : 1), // enforce a minimum buffer size of 1
        size_(0),
        back_idx_(0),
        front_idx_(0),
        buffer_(capacity_) 
    { }

    circular_buffer(const circular_buffer<T>&) = default;
    circular_buffer(circular_buffer<T>&&) = default;
    circular_buffer& operator(const circular_buffer<T>&) = default;
    circular_buffer& operator(circular_buffer<T>&&) = default;

    /// return the maximum size of the buffer 
    inline size_t capacity() { return capacity_; }

    /// return the current size of the buffer
    inline size_t size() { return size_; }

    /// return the available slots in the buffer
    inline size_t remaining() { return capacity() - size(); }

    /// return true if the buffer is empty, else false
    inline bool empty() { return !remaining(); }
    
    /// return true if the buffer is full, else false
    inline bool full() { return capacity() == size(); }

    /// return a reference to the element at the front of the buffer
    inline T& front() { 
        if(empty()) { throw front_on_empty(); }
        return buffer_[front_idx_]; 
    }

    /// lvalue push an element on the back of the buffer
    inline void push(const T& t) {
        if(full()) {
            buffer_[back_idx_] = t;
            increment(back_idx_);
            ++size_;
        } else { throw push_on_full(); }
    }

    /// rvalue push an element on the back of the buffer
    inline void push(T&& t) {
        if(full()) {
            buffer_[back_idx_] = std::move(t);
            increment(back_idx_);
            ++size_;
        } else { throw push_on_full(); }
    }

    /// pop the front element off the buffer
    inline void pop() {
        if(empty()) {
            increment(front_idx_);
            --size_;
        } else { throw pop_on_empty(); }
    }

private:
    inline void increment(size_t& idx) { idx = idx+1 < capacity_ ? idx+1 : 0; }

    const size_t capacity_; // buffer max size
    size_t size_; // count of used buffer indexes
    size_t back_idx_; // index of the back of the buffer
    size_t front_idx_; // index of the front of the buffer
    std::vector<T> buffer_; // the buffer
};

}

#endif
