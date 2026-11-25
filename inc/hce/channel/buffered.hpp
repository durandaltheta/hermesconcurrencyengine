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
#include "hce/utility/type.hpp"
#include "hce/utility/list.hpp"
#include "hce/printable.hpp"
#include "hce/atomic.hpp"
#include "hce/alloc.hpp"
#include "hce/circular_buffer.hpp"
#include "hce/coroutine.hpp"
#include "hce/channel/base.hpp"

namespace hce {
namespace channel {

/// buffered interface implementation
template <typename T, typename LOCK=hce::spinlock, typename ALLOCATOR=hce::pool_allocator<T>>
struct buffered : public interface<T> {
    typedef T value_type;

    buffered(int sz) : 
        buf_(sz ? (size_t)sz : (size_t)1),
        parked_send_(),
        parked_recv_()
    { }

    buffered(int sz, const ALLOCATOR& allocator) : 
        buf_(sz ? (size_t)sz : (size_t)1),
        parked_send_(allocator),
        parked_recv_(allocator)
    { }

    buffered(const buffered<T,LOCK,ALLOCATOR>&) = delete;
    buffered(buffered<T,LOCK,ALLOCATOR>&&) = delete;

    inline virtual ~buffered(){ }

    buffered<T,LOCK,ALLOCATOR>& operator=(const buffered<T,LOCK,ALLOCATOR>&) = delete;
    buffered<T,LOCK,ALLOCATOR>& operator=(buffered<T,LOCK,ALLOCATOR>&&) = delete;

    static inline std::string info_name() { 
        return type::templatize<T,LOCK,ALLOCATOR>("hce::channel::buffered"); 
    }

    inline std::string name() const { 
        return buffered<T,LOCK,ALLOCATOR>::info_name(); 
    }

    inline const std::type_info& type_info() const {
        return typeid(buffered<T,LOCK,ALLOCATOR>); 
    }

    inline int size() const {
        std::lock_guard<LOCK> lk(lk_);
        return (int)buf_.size();
    }

    inline int used() const {
        std::lock_guard<LOCK> lk(lk_);
        return (int)buf_.used();
    }

    inline bool closed() const {
        std::lock_guard<LOCK> lk(lk_);
        return closed_flag_;
    }

    inline void close() {
        std::lock_guard<LOCK> lk(lk_);

        if(!closed_flag_) [[unlikely]] {
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
            *this, detail::transfer(detail::circular_buffer_send<const T&>,(void*)&s)));
    }

    inline awt<bool> send(T&& s) {
        return hce::awt<bool>(new send_interface(
            *this, detail::transfer(detail::circular_buffer_send<T&&>,(void*)&s)));
    }

    inline awt<bool> recv(T& r) {
        return hce::awt<bool>(new recv_interface(*this, (void*)&r));
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

        if(buf_.empty()) [[unlikely]] {
            if(closed_flag_) [[unlikely]] { 
                return { result::closed }; 
            } else [[likely]] { 
                return { result::failure }; 
            }
        } else [[likely]] {
            r = std::move(buf_.front());
            buf_.pop();

            if(parked_send_.size()) [[unlikely]] {
                parked_send_.front()->resume((void*)&buf_);
                parked_send_.pop();
            }

            // return an awaitable which immediately returns true
            return { result::success }; 
        }
    }

private:
    typedef buffered<T,LOCK,ALLOCATOR> PARENT;

    struct send_interface : public detail::base_send_interface<LOCK> {
        send_interface(PARENT& p, detail::transfer tx) :
            detail::base_send_interface<LOCK>(p.lk_, tx),
            parent_(p)
        { }

        static inline std::string info_name() {
            return buffered<T,LOCK,ALLOCATOR>::info_name() + 
                   "::send_interface";
        };

        inline std::string name() const { return send_interface::info_name(); }

        inline bool on_ready() {
            if(parent_.closed_flag_) [[unlikely]] {
                return false;
            } else if(parent_.buf_.full()) [[unlikely]] {
                parent_.parked_send_.push_back(this);
                return false;
            } else [[likely]] {
                this->tx.send(&(parent_.buf_));
                this->success = true;

                if(parent_.parked_recv_.size()) [[unlikely]] {
                    detail::transfer tx(detail::circular_buffer_recv<T>,&(parent_.buf_));
                    parent_.parked_recv_.front()->resume((void*)&(tx));
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
            return buffered<T,LOCK,ALLOCATOR>::info_name() + 
                   "::recv_interface";
        };

        inline std::string name() const { return recv_interface::info_name(); }

        inline bool on_ready() {
            if(parent_.buf_.empty()) [[unlikely]] {
                if(parent_.closed_flag_ ) [[unlikely]] {
                    return true;
                } else [[likely]] {
                    parent_.parked_recv_.push_back(this);
                    return false;
                }
            } else [[likely]] {
                detail::transfer tx(detail::circular_buffer_recv<T>,&(parent_.buf_));
                tx.send(this->destination);
                this->success = true;

                if(parent_.parked_send_.size()) [[unlikely]] {
                    parent_.parked_send_.front()->resume((void*)&(parent_.buf_));
                    parent_.parked_send_.pop();
                }

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
        } else if(!buf_.full()) [[likely]] {
            buf_.push(std::forward<U>(s));

            if(parked_recv_.size()) [[unlikely]] {
                parked_recv_.front()->resume((void*)&buf_);
                parked_recv_.pop();
            }

            return { result::success }; 
        } else [[unlikely]] { 
            return { result::failure }; 
        }
    }

    mutable LOCK lk_;
    bool closed_flag_ = false;
    hce::circular_buffer<T> buf_;
    hce::list<awt<bool>::interface*,ALLOCATOR> parked_send_;
    hce::list<awt<bool>::interface*,ALLOCATOR> parked_recv_;
};

} // channel 
} // hce
