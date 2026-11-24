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
#include <optional>

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
namespace detail {
namespace unbuffered {

struct base : channel::interface {
    base();
    virtual ~base() = default;
    int size() const;
    int used() const;
    bool closed() const;
    void close();
    channel::operation request(const channel::request& r);
    void cancel(hce::awt<bool>::interface* op);

    struct selector : channel::base::selector {
        hce::awt<bool> request(const request& r);
        bool activate(hce::awt<bool>::interface* op);

    private:
        selector(interface<T>& p);
        interface<T>& parent;
        friend struct base;
    };

    channel::interface<T>::selector& select() { return selector_; }

protected:
    struct locker {
        void lock();
        void unlock();

        base& parent;
        friend struct base;
    };

    virtual void lock() = 0;
    virtual void unlock() = 0;
    virtual void complete_recv(void* resumed, void* opposite) = 0;
    virtual void complete_send(void* resumed, void* opposite) = 0;
    virtual hce::awt<bool>::interface* get_send_interface(const channel::request& r) = 0;
    virtual hce::awt<bool>::interface* get_recv_interface(const channel::request& r) = 0;
    virtual detail::transfer get_send_transfer(const channel::request& r) = 0;
    result try_send_(const channel::request& r);
    result try_recv_(const channel::request& r);

    bool closed_flag_ = false;
    hce::channel::list parked_send_;
    hce::channel::list parked_recv_;
    selector selector_;
    locker locker_;
    friend struct locker;
};

} // unbuffered
} // detail

template <typename T, typename LOCK = hce::spinlock>
struct unbuffered : detail::unbuffered::base {
    using self_type = unbuffered<T, LOCK>;
    using value_type = T;

    /// bring templates into the current object context
    using channel::interface<T>;

    unbuffered() : select_(*this) { }
    unbuffered(const self_type&) = delete;
    unbuffered(self_type&&) = delete;
    virtual ~unbuffered() = default;

    self_type& operator=(const self_type&) = delete;
    self_type& operator=(self_type&&) = delete;

    static inline std::string info_name() {
        return type::templatize<T, LOCK, ALLOCATOR>("hce::channel::unbuffered");
    }

    inline std::string name() const { return self_type::info_name(); }

    inline const std::type_info& type_info() const { return typeid(self_type); }

    inline void lock() { lk_.lock(); }
    inline void unlock() { lk_.unlock(); }

protected:
    hce::awt<bool>::interface* get_send_interface(const channel::request& r) {
        return hce::awt<bool>(new send_interface(&parent_, r));
    }

    hce::awt<bool>::interface* get_recv_interface(const channel::request& r) {
        return hce::awt<bool>(new recv_interface(&parent_, r));
    }

    detail::transfer get_send_transfer(const channel::request& r) {
        switch(r.op) {
            case channel::request::op_e::send_deep:
                return { detail::pointer_send<const T&>, r.value };
            case channel::request::op_e::send_shallow:
                return { detail::pointer_send<T&&>, r.value };
            case channel::request::op_e::try_send_deep:
                return { detail::pointer_send<const T&>, r.value };
            case channel::request::op_e::try_send_shallow:
                return { detail::pointer_send<T&&>, r.value };
            default: 
                return { };
        }
    }

private:
    void complete_recv(void* resumed, void* opposite) {
        ((recv_interface*)resume)->resume(static_cast<void*>(
            &(((send_interface)opposite)->tx)));
    }

    void complete_send(void* resumed, void* opposite) {
        ((send_interface*)resumed)->resume(
            ((recv_interface*)opposite)->destination);
    }

    struct send_interface : public detail::send::interface<LOCK> {
        send_interface(self_type& p, const channel::request& r) :
            detail::send::interface<LOCK>(p.lk_, r),
            parent_(p)
        {}

        inline bool on_ready() override {
            if(parent_.closed_flag_) { return true; }

            // we lost, return immediately
            if(this->group && this->group->is_claimed()) { return true; }

            while(parent_.parked_recv_.size()) {
                auto* ri = static_cast<recv_interface*>(parent_.parked_recv_.front());

                // claim both sides of the operation then complete it 
                if(ri->try_claim()) {
                    // This must succeed because we checked our group claim 
                    // before entering the loop, and this entire function is 
                    // behind the lock.
                    this->try_claim(); 
                    parent_.parked_recv_.pop();
                    parent_.complete_recv(ri, this);
                    this->success = true;
                    if(this->owner) { this->owner->on_won(this->index); }
                    return true;
                } else {
                    // receive lost, cancel it
                    parent_.parked_recv_.pop();
                    ri->resume(nullptr);
                }
            }

            parent_.parked_send_.push_back(this);
            return false;
        }
    private:
        self_type& parent_;
    };

    struct recv_interface : public detail::recv::interface<LOCK> {
        recv_interface(self_type& p, const channel::request& r) :
            detail::recv::interface<LOCK>(p.lk_, r),
            parent_(p)
        {}

        inline bool on_ready() override {
            if(parent_.closed_flag_) { return true; }

            // we lost, return immediately
            if(this->group && this->group->is_claimed()) { return true; }

            while(parent_.parked_send_.size()) {
                auto* si = static_cast<send_interface*>(parent_.parked_send_.front());
                
                if(si->try_claim()) {
                    this->try_claim(); 
                    parent_.parked_send_.pop();
                    parent_.complete_send(si, this);
                    this->success = true;
                    if(this->owner) { this->owner->on_won(this->index); }
                    return true;
                } else {
                    parent_.parked_send_.pop();
                    si->resume(nullptr);
                }
            }

            parent_.parked_recv_.push_back(this);
            return false;
        }

    private:
        self_type& parent_;
    };

    mutable LOCK lk_;
};

} // channel
} // hce
