//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#ifndef HERMES_COROUTINE_ENGINE_CIRCULAR_BUFFER
#define HERMES_COROUTINE_ENGINE_CIRCULAR_BUFFER

#include <cstdlib>
#include <algorithm>
#include <sstream>

// local
#include "logging.hpp"
#include "alloc.hpp"

namespace hce {

/**
 @brief a simple, highly efficient circular_buffer implementation

 This object is to serve the needs of this project and is not intended to 
 implement all the features of a more broad use container. It is designed for 
 speed and leaves error detection to the caller.

 Design Aims:
 - one time allocation
 - fast iteration (non-reentrant)
 - lazy T construction/destruction
 - 0 size buffer possible

 Design Limitations:
 - only supports FIFO push/front/pop 
 - no deep copy support
 - no iterators/reentrant iteration
 - no validity/error checking on emplace/push/pop
 - not directly resizable 

 "Resize" of a circular_buffer<T> is indirectly possible. Algorithm:
 - construct a local circular_buffer<T> of the same or greater size 
 - copy/move all elements from one buffer to the other. 
 - move swap the buffers

 IE:
 ```
 // new buffer has an equal or larger size than the old buffer for amortized growth
 hce::circular_buffer<T> new_buffer(old_buffer.size() * 2);

 while(old_buffer.used()) {
    // copy may be faster than swap for small types of T
    new_buffer.emplace(std::move(old_buffer.front()));
    old_buffer.pop();
 }

 // swap the buffers
 old_buffer = std::move(new_buffer);
 ```
 */
template <typename T>
struct circular_buffer : public printable {
    using value_type = T;

    constexpr circular_buffer(size_t sz) noexcept :
        size_(sz),
        buffer_(size_ ? hce::allocate<T>(size_) : nullptr)
    { 
        HCE_MIN_CONSTRUCTOR();
    }

    circular_buffer(const circular_buffer<T>& rhs) = delete;

    circular_buffer(circular_buffer<T>&& rhs) : size_(0), buffer_(nullptr) {
        HCE_MIN_CONSTRUCTOR(rhs.to_string() + "&&");
        swap_(std::move(rhs));
    }

    virtual ~circular_buffer() { 
        HCE_MIN_DESTRUCTOR(); 

        if(buffer_) [[likely]] {
            // destruct memory
            while(used()) [[likely]] { pop(); }

            // free buffer
            hce::deallocate(buffer_);
        }
    }
   
    circular_buffer<T>& operator=(const circular_buffer<T>& rhs) = delete;

    inline circular_buffer<T>& operator=(circular_buffer<T>&& rhs) {
        HCE_MIN_METHOD_ENTER("operator=", rhs.to_string() + "&&");
        swap_(std::move(rhs));
        return *this;
    }

    static inline std::string info_name() { 
        return type::templatize<T>("hce::circular_buffer"); 
    }

    inline std::string name() const { return circular_buffer<T>::info_name(); }

    inline std::string content() const {
        std::stringstream ss;
        ss << "size: " << size_ << ", used: " << used_;
        return ss.str();
    }

    /// return the maximum size of the buffer 
    inline size_t size() const { 
        HCE_TRACE_METHOD_ENTER("size");
        return size_; 
    }

    /// return the count of used elements of the buffer
    inline size_t used() const { 
        HCE_TRACE_METHOD_ENTER("used");
        return used_; 
    }

    /// return the available slots in the buffer
    inline size_t remaining() const { 
        HCE_TRACE_METHOD_ENTER("remaining");
        return size_ - used_; 
    }

    /// return true if the buffer is empty, else false
    inline bool empty() const { 
        HCE_TRACE_METHOD_ENTER("empty");
        return 0 == used_; 
    }
    
    /// return true if the buffer is full, else false
    inline bool full() const { 
        HCE_TRACE_METHOD_ENTER("full");
        return size_ == used_; 
    }

    /// return a reference to the element at the front of the buffer
    inline T& front() { 
        HCE_TRACE_METHOD_ENTER("front");
        return *(at_(front_idx_)); 
    }

    /**
     @brief emplace a new T 
     @param as... arguments for T constructor
     */
    template <typename... As>
    inline void emplace(As&&... as) {
        HCE_TRACE_METHOD_ENTER("emplace");

        // placement new construction
        new(at_(back_idx_)) T(std::forward<As>(as)...); 
        back_idx_ = (back_idx_ + 1) % size_;
        ++used_;
    }

    /// lvalue push an element on the back of the buffer
    inline void push(const T& t) {
        HCE_TRACE_METHOD_ENTER("push");
        emplace(t);
    }

    /// rvalue push an element on the back of the buffer
    inline void push(T&& t) {
        HCE_TRACE_METHOD_ENTER("push");
        emplace(std::move(t));
    }

    /// pop the front element off the buffer
    inline void pop() {
        HCE_TRACE_METHOD_ENTER("pop");

        // destruct the memory
        at_(front_idx_)->~T(); 
        front_idx_ = (front_idx_ + 1) % size_;
        --used_;
    }

private:
    // swap members
    inline void swap_(circular_buffer<T>&& rhs) {
        std::swap(size_, rhs.size_);
        std::swap(used_, rhs.used_);
        std::swap(front_idx_, rhs.front_idx_);
        std::swap(back_idx_, rhs.back_idx_);
        std::swap(buffer_, rhs.buffer_);
    }

    // return the pointer to T at a given index
    inline T* at_(size_t idx) const { return static_cast<T*>(buffer_) + idx; }

    size_t size_; // buffer size
    size_t used_ = 0; // count of used buffer indices
    size_t back_idx_ = 0; // index of the back of the queue
    size_t front_idx_ = 0; // index of the front of the queue
    void* buffer_; // allocated contiguous buffer
};

}

#endif
