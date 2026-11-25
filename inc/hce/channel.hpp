#pragma once
//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
// c++
#include <typeinfo>
#include <cstddef>
#include <mutex>
#include <functional>
#include <utility>
#include <vector>
#include <atomic>
#include <memory>

// local
#include "hce/utility/type.hpp"
#include "hce/printable.hpp"
#include "hce/memory.hpp"
#include "hce/alloc.hpp"
#include "hce/circular_buffer.hpp"
#include "hce/coroutine.hpp"
#include "hce/scheduler.hpp"
#include "hce/channel/base.hpp"
#include "hce/channel/unbuffered.hpp"
#if 0
#include "hce/channel/buffered.hpp"
#include "hce/channel/unlimited.hpp"
#endif

namespace hce {

/** 
 @brief communication object which can represent any object which implements channel::interface<T>

 This object is a sanitized `std::shared_ptr<hce::channel::interface<T>>`, with 
 helper construction functions and convenience operators. It is easily passed 
 around in coroutines without added template complexity or memory indirection 
 considerations (no `*` or `->` operator usage). Just make copies of an 
 instantiated `hce::chan<T>` and call its methods directly.

 It's useful to point out that implementations of `hce::channel::interface<T>` 
 can actually be used without this shim if desired. 
*/
template <typename T>
struct chan : public channel::interface<T> {
    using self_type = chan<T>;
    using value_type = T;

    /// bring templates into the current object context
    using channel::interface<T>;

    chan() = default;
    chan(const chan<T>& rhs) = default;
    chan(chan<T>&& rhs) = default;

    inline virtual ~chan(){ }

    chan<T>& operator=(const chan<T>& rhs) = default;
    chan<T>& operator=(chan<T>&& rhs) = default;

    static inline std::string info_name() { 
        return type::templatize<T>("hce::chan"); 
    }

    inline std::string name() const { return chan<T>::info_name(); }

    inline std::string content() const { 
        std::stringstream ss;
        ss << context_.get();
        return ss.str();
    }

    /// return a reference to the shared context
    inline std::shared_ptr<hce::channel::interface<T>>& context() {
        return context_;
    }

    /// return whether channel has an allocated context
    inline explicit operator bool() const {
        return (bool)(context_); 
    }

    /**
     @brief construct a chan's context with with one of several possible implementations 

     Constructed chan is:
     - sz == 0: unbuffered implementation
     - sz < 0: unlimited implementation
     - otherwise buffered implementation with a buffer of sz 

     Specify a LOCK type of `hce::lockfree` to make the implementation lockfree 
     (only safe when all instances of the chan are used from the same system 
     thread). Lockfree channels are a *very* fast way to communicate between two 
     coroutines running on the same scheduler (that is, the *same* thread of 
     execution).

     Alternatively, specify a LOCK type of `std::mutex` to potentially improve  
     communication congestion between a large number of system threads (not 
     coroutines running on a small number of threads). `std::mutex` will allow a
     blocked thread to wait on a condition instead of spinlocking.

     When in doubt, use the default `hce::spinlock`, it is quite performant in 
     all but extreme edgecases, because its usages are so brief and coroutine 
     context switching is so fast.

     @param sz the size of implementation buffer 
     @param as optional arguments for implementation constructor
     */
    template <typename LOCK=hce::spinlock, 
              typename... As>
    inline chan<T>& construct(int sz=0, As&&... as) {
#if 0
        if(sz == 0) {
#endif
            context_ = std::shared_ptr<channel::interface<T>>(
                static_cast<channel::interface<T>*>(
                    new hce::channel::unbuffered<T,LOCK>(std::forward<As>(as)...)));
#if 0
        } else if(sz < 0) {
            context_ = std::shared_ptr<channel::interface<T>>(
                static_cast<channel::interface<T>*>(
                    new hce::channel::unlimited<T,LOCK>(std::forward<As>(as)...)));
        } else {
            context_ = std::shared_ptr<channel::interface<T>>(
                static_cast<channel::interface<T>*>(
                    new hce::channel::buffered<T,LOCK>(sz, std::forward<As>(as)...)));
        }
#endif

        return *this;
    }

    /**
     @brief inline construct a new chan<T> and its context
     @return the constructed chan<T>
     */
    template <typename LOCK=hce::spinlock, 
              typename... As>
    inline static chan<T> make(As&&... as) {
        chan<T> ch;
        ch.construct<LOCK>(std::forward<As>(as)...);
        return ch;
    }
       
    inline const std::type_info& type_info() const {
        return context_->type_info(); 
    }

    inline int size() const {
        return context_->size(); 
    }

    inline int used() const {
        return context_->used(); 
    }

    inline bool closed() const {
        return context_->closed(); 
    }

    inline void close() {
        return context_->close(); 
    }
    
    inline hce::channel::operation request(const request& r) {
        return context_->request(r); 
    }
    
    inline hce::channel::interace<T>::selector& select() {
        return context_->select();
    }
   
    inline void cancel(hce::awt<bool>::interface* op) { 
        context_->cancel(op);
    }

private:
    std::shared_ptr<channel::interface<T>> context_;
}; 

}
