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

    unbuffered() :
        sync_(
            sync_t::guard_t{ 
                this, 
                [](void* v){ ((self_type*)v)->on_release_(); } 
            }
        )
    { }

    unbuffered(const ALLOCATOR& allocator) : 
        parked_send_(allocator),
        parked_recv_(allocator)
    { }

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
        return recv(r, nullptr);
    }

    inline awt<bool> recv(T& r, participant_t* p) {
        return hce::awt<bool>(new recv_interface(*this, (void*)&r, p));
    }

    inline hce::yield<result> try_send(const T& s) {
        return try_send_(s);
    }

    inline hce::yield<result> try_send(T&& s) {
        return try_send_(std::move(s));
    }

    inline hce::yield<result> try_recv(T& r) {
        return try_recv(r, nullptr);
    } 
    
    inline hce::yield<result> try_recv(T& r, participant_t* p) {
        std::lock_guard<LOCK> lk(lk_);

        if(closed_flag_) [[unlikely]] { 
            return { result::closed }; 
        } else if(!parked_send_.size() || guard_(p)) [[unlikely]] {
            return { result::failure }; 
        } else [[likely]] {
            parked_send_.front()->resume((void*)&r);
            if(p && sync_.active()) [[unlikely]] { sync_.release(p); }

            // return an awaitable which immediately returns true
            return { result::success }; 
        }
    }

    inline void participate(participant_t* p) { 
        std::lock_guard<LOCK> lk(lk_);

        sync_.insert(p); 
    }

    inline void withdraw(participant_t* p) {
        std::lock_guard<LOCK> lk(lk_);

        parked_recv_.remove_if([&](awt<bool>::interface* i) {
            if(p == ((recv_interface*)i)->key) {
                i->resume(nullptr);
                return true;
            } else {
                return false;
            }
        });

        sync_.erase(p);
    }

private:
    struct send_interface : 
        public detail::base_send_interface<LOCK> 
    {
        send_interface(self_type& p, detail::transfer tx) :
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
                this->success = resume_parked_receive_();
                return true;
            } else [[unlikely]] {
                parent_.parked_send_.push_back(this);
                return false;
            }
        }

    private:
        self_type& parent_;
    };

    struct recv_interface : public detail::base_recv_interface<LOCK> {
        recv_interface(self_type& p, void* destination, participant_t* k) :
            detail::base_recv_interface<LOCK>(p.lk_, destination),
            key(k),
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
                      (key = parent_.guard_(key))) [[unlikely]] {
                parent_.parked_recv_.push_back(this);
                return false;
            } else [[likely]] {
                parent_.parked_send_.front()->resume(this->destination);
                parent_.parked_send_.pop();
                this->success = true;
                return true;
            } 
        }

        // used for removal
        participant_t* key = nullptr;

    private:
        self_type& parent_;
    };
    
    template <typename U>
    inline hce::yield<result> try_send_(U&& s) {
        std::lock_guard<LOCK> lk(lk_);

        if(closed_flag_) [[unlikely]] { 
            return { result::closed }; 
        } else if(parked_recv_.size()) [[likely]] {
            detail::transfer tx(detail::pointer_send<U>,&s);

            if(resume_parked_receive_((void*)(&tx))) [[likely]] {
                return { result::success }; 
            } else [[unlikely]] {
                return { result::failure }; 
            }
        } else [[unlikely]] { return { result::failure }; }
    }

    inline bool resume_parked_receive_(void* v) {
        auto ri = (recv_interface*)(parked_recv_.front());

        if(sync_.active()) [[unlikely]] {
            // get the participant
            auto key = ri->key;
            
            if(sync_.try_claim_recv(key)) [[unlikely]] { 
                // can't resume right now, someone else acquired the guard
                return false;
            }

            ri->resume(v ? v : ri->destination);

            if(!key) { 
                // interface was not a participant, need to release immediately
                sync_.release(key); 
            }

            parked_recv_.pop();
            return true;
        } else {
            ri->resume(ri->destination);
            parked_recv_.pop();
            return true;
        }
    }

    /*
     If return value is non-nullptr, was guarded and failed to be acquired. 
     Return value can be stored as a removal key in a parked awaitable.
     */
    inline participant_t* guard_(participant_t* p) {
        // first check if we can do the fast path
        if(sync_.active() && sync_.try_claim_recv(p)) [[unlikely]] {
            return p;
        } else [[likely]] {
            return nullptr;
        }
    }

    inline void on_release_() {
        std::lock_guard<LOCK> lk(lk_);

        if(!closed_flag_ && parked_send_.size() && parked_recv_.size()) {
            resume_parked_receive_();
        }
    } 

    mutable LOCK lk_;
    bool closed_flag_ = false;
    hce::list<awt<bool>::interface*,ALLOCATOR> parked_send_;
    hce::list<awt<bool>::interface*,ALLOCATOR> parked_recv_;
    hce::channel::sync_t sync_;
};

} // channel 
} // hce
