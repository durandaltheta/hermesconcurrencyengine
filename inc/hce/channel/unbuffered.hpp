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
    channel::operation request(const channel::request& r);

protected:
    virtual hce::awt<bool>::interface* send_deep_(const channel::request&) = 0;
    virtual hce::awt<bool>::interface* send_shallow_(const channel::request&) = 0;
    virtual hce::awt<bool>::interface* recv_(const channel::request&) = 0;
    virtual result try_send_(const channel::request&) = 0;
    virtual result try_recv_(const channel::request&) = 0;
};

} // unbuffered
} // detail

template <typename T, typename LOCK = hce::spinlock, typename ALLOCATOR = hce::pool_allocator<T>>
struct unbuffered : detail::unbuffered::base {
    using self_type = unbuffered<T, LOCK, ALLOCATOR>;
    using value_type = T;

    unbuffered() : select_(*this) { }

    unbuffered(const ALLOCATOR& allocator)
        : parked_send_(allocator)
        , parked_recv_(allocator)
    {}

    unbuffered(const self_type&) = delete;
    unbuffered(self_type&&) = delete;
    virtual ~unbuffered() = default;

    self_type& operator=(const self_type&) = delete;
    self_type& operator=(self_type&&) = delete;

    static inline std::string info_name() {
        return type::templatize<T, LOCK, ALLOCATOR>("hce::channel::grok::unbuffered");
    }
    inline std::string name() const { return self_type::info_name(); }

    inline const std::type_info& type_info() const { return typeid(self_type); }
    inline int size() const { return 0; }
    inline int used() const { return 0; }

    inline bool closed() const {
        std::lock_guard<LOCK> lk(lk_);
        return closed_flag_;
    }

    inline void close() {
        std::lock_guard<LOCK> lk(lk_);
        if(closed_flag_) return;
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

    struct selector : channel::interface<T>::selector {
        inline hce::awt<bool> send(const request& r) {
            return hce::awt<bool>(new send_interface(
                &parent_, 
                r.move
                    ? detail::transfer(detail::pointer_send<T&&>, r.value)
                    : detail::transfer(detail::pointer_send<const T&>, r.value), 
                r.group, 
                r.index, 
                r.owner));
        }

        inline hce::awt<bool> recv(const request& r) {
            return hce::awt<bool>(new recv_interface(
                &parent_, 
                r.value,
                r.group, 
                r.index, 
                r.owner));
        }

        // Returns true if the op parked, false if it completed immediately
        // (matched or closed).
        bool activate(hce::awt<bool>::interface* op) {
            if(!op) { return false; }

            std::lock_guard<LOCK> lk(lk_);
            return !static_cast<recv_interface*>(op)->on_ready();
        }

    private:
        selector(interface<T>& p) : parent(p) { }
        interface<T>& parent;
    };

    channel::interface<T>::selector& select() { return selector_; }

    void cancel(hce::awt<bool>::interface* op) {
        if(!op){ return; }

        std::lock_guard<LOCK> lk(lk_);
        bool found = false;

        parked_send_.remove_if([&](hce::awt<bool>::interface* i) {
            if(i == op) { found = true; return true; }
            return false;
        });

        if(!found) {
            parked_recv_.remove_if([&](hce::awt<bool>::interface* i) {
                if(i == op) { found = true; return true; }
                return false;
            });
        }

        if(found) op->resume(nullptr);
    }

protected:
    result try_send_
    template <typename U>
    inline result try_send_(U&& s) {
        std::lock_guard<LOCK> lk(lk_);
        if(closed_flag_) { return result::closed; }

        detail::transfer tx(detail::pointer_send<U>, &s);

        while(parked_recv_.size()) {
            auto* ri = static_cast<recv_interface*>(parked_recv_.front());
            if(ri->group && ri->group->is_claimed()) {
                parked_recv_.pop();
                ri->resume(nullptr);
                continue;
            }
            if(ri->try_claim()) {
                parked_recv_.pop();
                ri->resume(static_cast<void*>(&tx));
                return result::success;
            }
            parked_recv_.pop();
            ri->resume(nullptr);
        }

        return result::failure;
    }

    inline result try_recv_(T& r) { 
        std::lock_guard<LOCK> lk(lk_);

        if(closed_flag_) { return result::closed; }

        while(parked_send_.size()) {
            auto* si = static_cast<send_interface*>(parked_send_.front());

            if(si->group && si->group->is_claimed()) {
                parked_send_.pop();
                si->resume(nullptr);
                continue;
            }

            if(si->try_claim()) {
                parked_send_.pop();
                si->resume(static_cast<void*>(&r));
                return result::success;
            }

            parked_send_.pop();
            si->resume(nullptr);
        }
        return result::failure;
    }

private:

    struct send_interface : public detail::base_send_interface<LOCK> {
        send_interface(self_type& p, detail::transfer tx, select::group* g, int idx)
            : detail::base_send_interface<LOCK>(p.lk_, tx, g, idx)
            , parent_(p)
        {}

        inline bool on_ready() override {
            if(parent_.closed_flag_) { return true; }

            while(parent_.parked_recv_.size()) {
                auto* ri = static_cast<recv_interface*>(parent_.parked_recv_.front());

                if(ri->group && ri->group->is_claimed()) {
                    parent_.parked_recv_.pop();
                    ri->resume(nullptr);
                    continue;
                }

                if(ri->try_claim() && this->try_claim()) {
                    parent_.parked_recv_.pop();
                    ri->resume(static_cast<void*>(&this->tx));
                    this->success = true;
                    return true;
                }
                parent_.parked_recv_.pop();
                ri->resume(nullptr);
            }

            parent_.parked_send_.push_back(this);
            return false;
        }
    private:
        self_type& parent_;
    };

    struct recv_interface : public detail::base_recv_interface<LOCK> {
        recv_interface(self_type& p, void* destination, select::group* g,
                       int idx, select::notify* own)
            : detail::base_recv_interface<LOCK>(p.lk_, destination, g, idx, own)
            , parent_(p)
        {}

        inline bool on_ready() override {
            if(parent_.closed_flag_) { return true; }

            while(parent_.parked_send_.size()) {
                auto* si = static_cast<send_interface*>(parent_.parked_send_.front());
                if(si->group && si->group->is_claimed()) {
                    parent_.parked_send_.pop();
                    si->resume(nullptr);
                    continue;
                }

                if(si->try_claim() && this->try_claim()) {
                    parent_.parked_send_.pop();
                    si->resume(this->destination);
                    this->success = true;
                    if(this->owner) this->owner->on_won(this->index);
                    return true;
                }

                if(this->group && this->group->is_claimed()) {
                    parent_.parked_send_.pop();
                    si->resume(nullptr);
                    return true;
                }
                parent_.parked_send_.pop();
                si->resume(nullptr);
            }

            if(this->group && this->group->is_claimed()) { return true; }

            parent_.parked_recv_.push_back(this);
            return false;
        }
    private:
        self_type& parent_;
    };

    selector select_;
    mutable LOCK lk_;
    bool closed_flag_ = false;
    hce::list<hce::awt<bool>::interface*, ALLOCATOR> parked_send_;
    hce::list<hce::awt<bool>::interface*, ALLOCATOR> parked_recv_;
};

} // channel
} // hce
