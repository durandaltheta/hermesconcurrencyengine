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
#include "hce/scheduler.hpp"
#include "hce/channel/base.hpp"

namespace hce {
namespace channel {

/**
 @brief unlimited interface implementation, with has no max fixed size buffer 
 */
template <typename T, typename LOCK=hce::spinlock, typename ALLOCATOR=hce::pool_allocator<T>>
struct unlimited : public interface<T> {
    typedef unlimited<T,LOCK,ALLOCATOR> self_type;
    typedef T value_type;

    unlimited() { 
    }

    unlimited(const ALLOCATOR& allocator) : 
        queue_(allocator),
        parked_recv_(allocator)
    { 
    }

    unlimited(const unlimited<T,LOCK,ALLOCATOR>&) = delete;
    unlimited(unlimited<T,LOCK,ALLOCATOR>&&) = delete;

    inline virtual ~unlimited(){ }

    unlimited<T,LOCK,ALLOCATOR>& operator=(const unlimited<T,LOCK,ALLOCATOR>&) = delete;
    unlimited<T,LOCK,ALLOCATOR>& operator=(unlimited<T,LOCK,ALLOCATOR>&&) = delete;

    static inline std::string info_name() { 
        return type::templatize<T,LOCK,ALLOCATOR>("hce::channel::unlimited"); 
    }

    inline std::string name() const { 
        return unlimited<T,LOCK,ALLOCATOR>::info_name(); 
    }

    inline const std::type_info& type_info() const {
        return typeid(unlimited<T,LOCK,ALLOCATOR>); 
    }

    inline int size() const {
        return -1;
    }

    inline int used() const {
        std::lock_guard<LOCK> lk(lk_);
        return (int)queue_.size();
    }

    inline bool closed() const {
        std::lock_guard<LOCK> lk(lk_);
        return closed_flag_;
    }

    inline void close() {
        std::lock_guard<LOCK> lk(lk_);

        if(!closed_flag_) [[unlikely]] {
            closed_flag_ = true;

            while(parked_recv_.size()) { 
                parked_recv_.front()->resume(nullptr); 
                parked_recv_.pop();
            }
        }
    }

    inline awt<bool> send(const T& s) {
        return awt<bool>(new send_interface(
            *this, 
            detail::transfer(detail::list_send<const T&,hce::list<T,ALLOCATOR>>,&s)));
    }

    inline awt<bool> send(T&& s) {
        return awt<bool>(new send_interface(
            *this, 
            detail::transfer(detail::list_send<T&&,hce::list<T,ALLOCATOR>>,&s)));
    }

    /**
     Buffered receives will succeed, even if the channel is closed, as long 
     as values are available for retrieval from the internal buffer.
     */
    inline awt<bool> recv(T& r) {
        return awt<bool>(new recv_interface(*this, (void*)&r));
    }

    inline hce::yield<result> try_send(const T& t) {
        return try_send_(t);
    }

    inline hce::yield<result> try_send(T&& t) {
        return try_send_(std::move(t));
    }

    /**
     Buffered receives will succeed, even if the channel is closed, as long 
     as values are available for retrieval from the internal buffer.
     */
    inline hce::yield<result> try_recv(T& r) {
        std::lock_guard<LOCK> lk(lk_);

        if(queue_.empty()) [[unlikely]] {
            if(closed_flag_) [[unlikely]] { 
                return { result::closed }; 
            } else [[likely]] { 
                return { result::failure }; 
            }
        } else [[likely]] {
            if(guarded_()) [[unlikely]] {
                return { result::failure }; 
            } else {
                r = std::move(queue_.front());
                queue_.pop();

                // return an awaitable which immediately returns true
                return { result::success }; 
            }
        }
    }

    inline void set_guard(guard* g) { 
        std::lock_guard<LOCK> lk(lk_);
        
        guard_ = g; 

        if(guard_) {
            guard_->children.emplace_back(
                this,
                [](void* v) { ((self_type*)v)->unblock_(); },
                [](void* v) { ((self_type*)v)->remove_(); }
            );
        }
    }

private:
    typedef unlimited<T,LOCK,ALLOCATOR> PARENT;

    struct send_interface : public detail::base_send_interface<LOCK> {
        send_interface(PARENT& p, detail::transfer tx) :
            detail::base_send_interface<LOCK>(p.lk_, tx),
            parent_(p)
        { }

        static inline std::string info_name() {
            return unlimited<T,LOCK,ALLOCATOR>::info_name() + 
                   "::send_interface";
        };

        inline std::string name() const { return send_interface::info_name(); }

        inline bool on_ready() { 
            if(parent_.closed_flag_) [[unlikely]] {
                return true;
            } else [[likely]] {
                this->tx.send(&(parent_.queue_));
                this->success = true;

                if(parent_.parked_recv_.size()) [[unlikely]] {
                    detail::transfer tx(&detail::list_recv<T,hce::list<T,ALLOCATOR>>,&(parent_.queue_));
                    parent_.parked_recv_.front()->resume((void*)&tx);
                    parent_.parked_recv_.pop();
                }

                return true;
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
            return unlimited<T,LOCK,ALLOCATOR>::info_name() + 
                   "::recv_interface";
        };

        inline std::string name() const { return recv_interface::info_name(); }

        inline bool on_ready() {
            if(parent_.queue_.empty()) [[unlikely]] {
                if(parent_.closed_flag_ ) [[unlikely]] {
                    return true;
                } else [[likely]] {
                    parent_.parked_recv_.push_back(this);
                    return false;
                }
            } else if(key = parent_.guarded_()) [[unlikely]] {
                parent_.parked_recv_.push_back(this);
                return false;
            } else [[likely]] {
                detail::transfer tx(&detail::list_recv<T,hce::list<T,ALLOCATOR>>,&(parent_.queue_));
                tx.send(this->destination);
                this->success = true;
                return true;
            }
        }

        // used for removal
        void* key = nullptr;

    private:
        PARENT& parent_;
    };
    
    template <typename U>
    inline hce::yield<result> try_send_(U&& s) {
        std::lock_guard<LOCK> lk(lk_);

        if(closed_flag_) [[unlikely]] { 
            return { result::closed }; 
        } else [[likely]] {
            queue_.push_back(std::forward<U>(s));

            if(parked_recv_.size()) [[unlikely]] {
                detail::transfer tx(&detail::list_recv<T,hce::list<T,ALLOCATOR>>, &queue_);
                parked_recv_.front()->resume((void*)&tx);
                parked_recv_.pop();
            }

            // return an awaitable which immediately returns true
            return { result::success }; 
        } 
    }

    // If return value is non-nullptr, it was guarded. Return value can be
    // stored as a removal key in a parked awaitable.
    inline void* guarded_() {
        if(guard_ && 
           !guard_->parent.try_claim_recv(parent_.guard_->parent.ctx)) [[unlikely]] {
            return guard_;
        } else {
            return nullptr;
        }
    }

    inline void unblock_() {
        std::lock_guard<LOCK> lk(lk_);

        if(!closed_flag_ && parked_recv_.size()) {
            if(guard_ && !guard_->parent.try_claim_recv(guard_->parent.ctx)) {
                return;
            }

            auto awt = parked_recv_.front();
            parked_recv_.pop();
            awt->resume((void*)&queue_);
        }
    }

    inline void remove_() {
        std::lock_guard<LOCK> lk(lk_);

        if(guard_) {
            parked_recv_.remove_if([&](awt<bool>::interface* i) {
                return guard_ == ((recv_interface*)i)->key;
            });
        }
    } 

    mutable LOCK lk_;
    bool closed_flag_ = false;
    hce::list<T,ALLOCATOR> queue_;
    hce::list<awt<bool>::interface*,ALLOCATOR> parked_recv_;
    guard* guard_ = nullptr;
};

} // channel 
} // hce
