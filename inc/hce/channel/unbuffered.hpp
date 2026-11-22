#pragma once
//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
// c++
#include <typeinfo>
#include <mutex>
#include <functional>
#include <utility>
#include <vector>
#include <atomic>

// local
#include "hce/utility.hpp"
#include "hce/printable.hpp"
#include "hce/type.hpp"
#include "hce/atomic.hpp"
#include "hce/alloc.hpp"
#include "hce/list.hpp"
#include "hce/coroutine.hpp"
#include "hce/channel/base.hpp"

namespace hce {
namespace channel {

/// unbuffered interface implementation
template <typename T, typename LOCK=hce::spinlock, typename ALLOCATOR=hce::pool_allocator<T>>
struct unbuffered : public interface<T> {
    typedef unbuffered<T,LOCK,ALLOCATOR> self_type;
    typedef T value_type;

    unbuffered() { }

    unbuffered(const ALLOCATOR& allocator) : 
        parked_send_(allocator),
        parked_recv_(allocator)
    { 
    }

    unbuffered(const unbuffered<T,LOCK,ALLOCATOR>&) = delete;
    unbuffered(unbuffered<T,LOCK,ALLOCATOR>&&) = delete;
    
    inline virtual ~unbuffered(){ }

    unbuffered<T,LOCK,ALLOCATOR>& operator=(const unbuffered<T,LOCK,ALLOCATOR>&) = delete;
    unbuffered<T,LOCK,ALLOCATOR>& operator=(unbuffered<T,LOCK,ALLOCATOR>&&) = delete;

    static inline std::string info_name() { 
        return type::templatize<T,LOCK,ALLOCATOR>("hce::channel::unbuffered"); 
    }

    inline std::string name() const { 
        return unbuffered<T,LOCK,ALLOCATOR>::info_name(); 
    }

    inline const std::type_info& type_info() const {
        return typeid(unbuffered<T,LOCK,ALLOCATOR>); 
    }

    inline int size() const { 
        return 0; 
    }

    inline int used() const { 
        return 0; 
    }

    inline bool closed() const {
        std::lock_guard<LOCK> lk(lk_);
        return closed_flag_;
    }

    inline void close() {
        std::lock_guard<LOCK> lk(lk_);
        if(!closed_flag_) [[likely]] {
            closed_flag_ = true;

            while(parked_send_.size()) { 
                parked_send_.front()->resume(nullptr); 
                parked_send_.pop();
            }

            while(parked_recv_.size()) { 
                parked_recv_.front()->resume(nullptr); 
                parked_recv_.pop();
            }
        }
    }

    inline awt<bool> send(const T& s) {
        return hce::awt<bool>(new send_interface(
            *this, detail::transfer(detail::pointer_send<const T&>,&s)));
    }

    inline awt<bool> send(T&& s) {
        return hce::awt<bool>(new send_interface(
            *this, detail::transfer(detail::pointer_send<T&&>,&s)));
    }

    inline awt<bool> recv(T& r) {
        return hce::awt<bool>(new recv_interface(*this, (void*)&r));
    }

    inline hce::yield<result> try_send(const T& s) {
        return try_send_(s);
    }

    inline hce::yield<result> try_send(T&& s) {
        return try_send_(std::move(s));
    }

    inline hce::yield<result> try_recv(T& r) {
        std::lock_guard<LOCK> lk(lk_);

        if(closed_flag_) [[unlikely]] { 
            return { result::closed }; 
        } else if(!parked_send_.size() ||
                  (guard_ && 
                   !guard_->parent.try_claim_recv(guard_->parent.ctx))) [[unlikely]] {
            return { result::failure }; 
        } else [[likely]] {
            parked_send_.front()->resume((void*)&r);

            // return an awaitable which immediately returns true
            return { result::success }; 
        }
    }

    inline void set_guard(guard* g) { 
        std::lock_guard<LOCK> lk(lk_);
        
        guard_ = g; 

        if(guard_) {
            guard_->children.emplace_back(
                this,
                [](void* v) { ((self_type*)v)->unblock_(); }
            );
        }
    }

private:
    typedef unbuffered<T,LOCK,ALLOCATOR> PARENT;

    struct send_interface : 
        public detail::base_send_interface<LOCK> 
    {
        send_interface(PARENT& p, detail::transfer tx) :
            detail::base_send_interface<LOCK>(p.lk_, tx),
            parent_(p)
        { }

        static inline std::string info_name() {
            return unbuffered<T,LOCK,ALLOCATOR>::info_name() + 
                   "::send_interface";
        };

        inline std::string name() const { return send_interface::info_name(); }

        inline bool on_ready() {
            if(parent_.closed_flag_) [[unlikely]] {
                return true;
            } else if(parent_.parked_recv_.size()) [[likely]] {
                parent_.parked_recv_.front()->resume((void*)&(this->tx));
                parent_.parked_recv_.pop();
                this->success = true;
                return true;
            } else [[unlikely]] {
                parent_.parked_send_.push_back(this);
                return false;
            }
        }

    private:
        PARENT& parent_;
    };

    struct recv_interface : public detail::base_recv_interface<LOCK> {
        recv_interface(PARENT& p, void* destination) :
            detail::base_recv_interface<LOCK>(p.lk_, destination),
            parent_(p)
        { }

        static inline std::string info_name() {
            return unbuffered<T,LOCK,ALLOCATOR>::info_name() + 
                   "::recv_interface";
        };

        inline std::string name() const { return recv_interface::info_name(); }

        inline bool on_ready() {
            if(parent_.closed_flag_) [[unlikely]] { 
                return true;
            } else if(!parent_.parked_send_.size() ||
                      (parent_.guard_ && 
                       !parent_.guard_->parent.try_claim_recv(parent_.guard_->parent.ctx))) [[unlikely]] {
                parent_.parked_recv_.push_back(this);
                return false;
            } else [[likely]] {
                parent_.parked_send_.front()->resume(this->destination);
                parent_.parked_send_.pop();
                this->success = true;
                return true;
            } 
        }

    private:
        PARENT& parent_;
    };
    
    template <typename U>
    inline hce::yield<result> try_send_(U&& s) {
        std::lock_guard<LOCK> lk(lk_);

        if(closed_flag_) [[unlikely]] { 
            return { result::closed }; 
        } else if(parked_recv_.size()) [[likely]] {
            detail::transfer tx(detail::pointer_send<U>,&s);
            parked_recv_.front()->resume((void*)(&tx));
            parked_recv_.pop();

            return { result::success }; 
        } else [[unlikely]] { return { result::failure }; }
    }

    inline void unblock_() {
        std::lock_guard<LOCK> lk(lk_);

        if(!closed_flag_ && parked_send_.size() && parked_recv_.size()) {
            if(guard_ && !guard_->parent.try_claim_recv(guard_->parent.ctx)) {
                return;
            }

            auto awt = parked_recv_.front();
            parked_recv_.pop();
            awt->resume(((recv_interface*)awt)->destination);
        }
    }

    mutable LOCK lk_;
    bool closed_flag_ = false;
    hce::list<awt<bool>::interface*,ALLOCATOR> parked_send_;
    hce::list<awt<bool>::interface*,ALLOCATOR> parked_recv_;
    guard* guard_ = nullptr;
};

} // channel 
} // hce
