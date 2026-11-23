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

/// unlimited interface implementation
template <typename T, typename LOCK=hce::spinlock, typename ALLOCATOR=hce::pool_allocator<T>>
struct unlimited : public interface<T> {
    typedef T value_type;

    unlimited() { }

    unlimited(const ALLOCATOR& allocator) : 
        queue_(allocator),
        parked_recv_(allocator)
    { }

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
            r = std::move(queue_.front());
            queue_.pop();

            // return an awaitable which immediately returns true
            return { result::success }; 
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
            } else [[likely]] {
                detail::transfer tx(&detail::list_recv<T,hce::list<T,ALLOCATOR>>,&(parent_.queue_));
                tx.send(this->destination);
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

    mutable LOCK lk_;
    bool closed_flag_ = false;
    hce::list<T,ALLOCATOR> queue_;

    // send() never blocks, so only has parked recv queue
    hce::list<awt<bool>::interface*,ALLOCATOR> parked_recv_;
};

} // channel 
} // hce
