//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#ifndef __HERMES_COROUTINE_ENGINE_SYNCHRONIZED_list__
#define __HERMES_COROUTINE_ENGINE_SYNCHRONIZED_list__

#include <mutex>
#include <condition_variable>

#include "logging.hpp"
#include "atomic.hpp"
#include "list.hpp"

namespace hce {

/**
 @brief an atomically synchronized queue for template type T 

 Very similar to hce::queue<T>, except that operations are atomically 
 synchronized for calling on different threads.

 Conceptually similar to the more robust hce::channel<T>, this simpler type is 
 not coroutine safe and is intended for comunication between system threads.
 */
template <typename T, typename Allocator = hce::pool_allocator<T>>
struct synchronized_list : public printable {
    using value_type = T;

    synchronized_list() : 
        closed_(false), 
        waiting_(false)
    { 
        HCE_MIN_CONSTRUCTOR(); 
    }

    synchronized_list(const Allocator& allocator) : 
        closed_(false), 
        waiting_(false),
        list_(allocator) { 
        HCE_MIN_CONSTRUCTOR(allocator); 
    }
    
    synchronized_list(const synchronized_list<T>&) = delete;
    synchronized_list(synchronized_list<T>&&) = delete;

    static inline std::string info_name() { 
        return type::templatize<T>("hce::synchronized_list"); 
    }

    inline std::string name() const { 
        return synchronized_list<T>::info_name(); 
    }

    inline synchronized_list<T>& operator=(const synchronized_list<T>&) = delete;
    inline synchronized_list<T>& operator=(synchronized_list<T>&&) = delete;

    /**
     @return the current length of the queue
     */
    inline size_t size() const { 
        std::lock_guard<hce::spinlock> lk(lk_);
        return list_.size();
    }

    /**
     @return true if empty, else false
     */
    inline bool empty() const { 
        std::lock_guard<hce::spinlock> lk(lk_);
        return list_.empty();
    }

    /**
     @brief close the queue 

     This operations causes future emplace()/push_back() operations to fail, and 
     eventually cause pop() to fail once the queue is empty.
     */
    inline void close() { 
        bool waiting = false;

        {
            std::lock_guard<hce::spinlock> lk(lk_);

            if(!closed_) {
                closed_ = true;
                waiting = waiting_;
                waiting_ = false;
            }
        }

        if(waiting) [[likely]] { cv_.notify_all(); }
    }
   
    /**
     @return true if the queue is closed, else false
     */
    inline bool closed() { 
        std::lock_guard<hce::spinlock> lk(lk_);
        return closed_;
    }

    /** 
     @brief emplace a value into the back of the queue

     This operation never blocks

     @param as arguments for T constructor
     @return false if the queue is closed, else true 
     */
    template <typename... As> 
    inline bool emplace_back(As&&... as) {
        bool waiting = false; 

        {
            std::lock_guard<hce::spinlock> lk(lk_);

            // will immediately fail if synchronized_list is closed
            if(closed_) [[unlikely]] {
                return false;
            } else [[likely]] {
                list_.emplace_back(std::forward<As>(as)...);
                waiting = waiting_;
                waiting_ = false;
            }
        }

        if(waiting) [[likely]] { cv_.notify_one(); }
        return true;
    }

    /** 
     @brief emplace a value into the front of the queue

     This operation never blocks

     @param as arguments for T constructor
     @return false if the queue is closed, else true 
     */
    template <typename... As> 
    inline bool emplace_front(As&&... as) {
        bool waiting = false; 

        {
            std::lock_guard<hce::spinlock> lk(lk_);

            // will immediately fail if synchronized_list is closed
            if(closed_) [[unlikely]] {
                return false;
            } else [[likely]] {
                list_.emplace_front(std::forward<As>(as)...);
                waiting = waiting_;
                waiting_ = false;
            }
        }

        if(waiting) [[likely]] { cv_.notify_one(); }
        return true;
    }

    /** 
     @brief lvalue push a value onto the back of the queue

     This operation never blocks.

     @param t a value
     @return false if the queue is closed, else true 
     */
    inline bool push_back(const T& t) { 
        return emplace_back(t); 
    }
    
    /** 
     @brief rvalue push a value onto the back of the queue

     This operation never blocks.

     @param t a value
     @return false if the queue is closed, else true 
     */
    inline bool push_back(T&& t) { 
        return emplace_back(std::move(t)); 
    }

    /** 
     @brief lvalue push a value onto the front of the queue

     This operation never blocks.

     @param t a value
     @return false if the queue is closed, else true 
     */
    inline bool push_front(const T& t) { 
        return emplace_front(t); 
    }

    /** 
     @brief rvalue push a value onto the front of the queue

     This operation never blocks.

     @param t a value
     @return false if the queue is closed, else true 
     */
    inline bool push_front(T&& t) { 
        return emplace_front(std::move(t)); 
    }

    /**
     @brief retrieve and pop a value off the front queue 

     This operation will block until a value is available.

     This operation combines `hce::queue::front()` and `hce::queue::pop()` 
     in order be synchronized.

     @param t memory to write the popped value to
     @return false if the queue is closed, else true 
     */
    inline bool pop(T& t) {
        std::unique_lock<hce::spinlock> lk(lk_);

        // will always succeed as long as operations are available
        while(!list_.size()) {
            if(closed_) [[unlikely]] {
                return false;
            } else [[likely]] {
                waiting_ = true;
                cv_.wait(lk);
            }
        }

        t = std::move(list_.front());
        list_.pop();
        return true;
    }

private:
    mutable hce::spinlock lk_;
    bool closed_;
    bool waiting_;
    std::condition_variable_any cv_;
    hce::list<T,Allocator> list_;
};

}

#endif
