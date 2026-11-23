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

late <typename T, typename LOCK = hce::spinlock, typename ALLOCATOR = hce::pool_allocator<T>>
struct unbuffered : channel::interface {
    using self_type = unbuffered<T, LOCK, ALLOCATOR>;
    using value_type = T;

    unbuffered() = default;
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

    inline hce::awt<bool> send(const T& s) {
        return hce::awt<bool>(new send_interface(
            *this, detail::transfer(detail::pointer_send<const T&>, &s), nullptr, -1));
    }

    inline hce::awt<bool> send(T&& s) {
        return hce::awt<bool>(new send_interface(
            *this, detail::transfer(detail::pointer_send<T&&>, &s), nullptr, -1));
    }

    inline hce::awt<bool> recv(T& r) { 
        return hce::awt<bool>(new recv_interface(
            *this, static_cast<void*>(&r), nullptr, -1, nullptr));
    }

    inline hce::yield<result> try_send(const T& s) { return try_send_(s); }
    inline hce::yield<result> try_send(T&& s) { return try_send_(std::move(s)); }

    inline hce::yield<result> try_recv(T& r) { 
        std::lock_guard<LOCK> lk(lk_);

        if(closed_flag_) { return { result::closed }; }

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
                return { result::success };
            }

            parked_send_.pop();
            si->resume(nullptr);
        }
        return { result::failure };
    }

    // Build a select-recv awaitable but do not start it. The select
    // calls activate_select to register it under the channel lock.
    inline hce::awt<bool> make_select_recv(
            T& r, 
            select_group& g, 
            int index,
            select_notify* owner) 
    {
        return hce::awt<bool>(new recv_interface(
            *this, static_cast<void*>(&r), &g, index, owner));
    }

    // Returns true if the op parked, false if it completed immediately
    // (matched or closed).
    bool activate_select(hce::awt<bool>::interface* op) {
        if(!op) { return false; }
        std::lock_guard<LOCK> lk(lk_);
        return !static_cast<recv_interface*>(op)->on_ready();
    }

    void cancel(hce::awt<bool>::interface* op) {
        if(!op) { return; }

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

private:
    template <typename U>
    inline hce::yield<result> try_send_(U&& s) {
        std::lock_guard<LOCK> lk(lk_);
        if(closed_flag_) return { result::closed };
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
                return { result::success };
            }
            parked_recv_.pop();
            ri->resume(nullptr);
        }
        return { result::failure };
    }

    struct send_interface : public detail::base_send_interface<LOCK> {
        send_interface(self_type& p, detail::transfer tx, select_group* g, int idx)
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
        recv_interface(self_type& p, void* destination, select_group* g,
                       int idx, select_notify* own)
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

    mutable LOCK lk_;
    bool closed_flag_ = false;
    hce::list<hce::awt<bool>::interface*, ALLOCATOR> parked_send_;
    hce::list<hce::awt<bool>::interface*, ALLOCATOR> parked_recv_;
};

namespace detail {

struct case_base {
    virtual ~case_base() = default;
    virtual bool activate(select_notify* owner) = 0;
    virtual void cancel() = 0;
    virtual int index() const = 0;
};

template <typename T>
struct recv_case : case_base {
    unbuffered<T>* ch = nullptr;
    T* dest = nullptr;
    select_group* group = nullptr;
    int idx = -1;
    hce::awt<bool> op;
    hce::awt<bool>::interface* iface = nullptr;

    recv_case(unbuffered<T>& c, T& d, select_group& g, int i)
        : ch(&c), dest(&d), group(&g), idx(i) {}

    bool activate(select_notify* owner) override {
        op = ch->make_select_recv(*dest, *group, idx, owner);
        iface = dynamic_cast<hce::awt<bool>::interface*>(&op.implementation());
        return ch->activate_select(iface);
    }

    void cancel() override {
        if(iface && ch){ 
            hce::awt<bool> awt(iface);
            ch->cancel(awt); 
            awt.release();
        }
    }

    int index() const override { return idx; }
};

struct select_state {
    hce::spinlock lk;
    select_group* group = nullptr; // owned by the select object
    std::vector<std::unique_ptr<case_base>> cases;
    int winner = -1;
    bool parked = false;
    bool completed = false;
};

struct select_impl : 
    hce::awaitable::lockable<hce::spinlock, hce::awt_interface<int>>, 
    select_notify
{
    std::unique_ptr<select_state> st;

    explicit select_impl(std::unique_ptr<select_state> s):
          hce::awaitable::lockable<hce::spinlock, hce::awt_interface<int>>(
              s->lk,
              hce::awaitable::await::policy::defer,
              hce::awaitable::resume::policy::no_lock)
        , st(std::move(s))
    {}

    static inline std::string info_name() { return "hce::channel::grok::select_impl"; }
    inline std::string name() const { return info_name(); }

    bool on_ready() override {
        int parked_count = 0;

        for(auto& c : st->cases) {
            if(c->activate(this)) { ++parked_count; }
        }

        st->parked = true;

        if(st->group->is_claimed()) {
            st->winner = st->group->winner_index();
            cancel_losers();
            st->completed = true;
            return true;
        }

        if(parked_count == 0) {
            st->winner = -1;
            st->completed = true;
            return true;
        }

        return false;
    }

    void on_resume(void*) override {
        if(st->winner < 0) { st->winner = st->group->winner_index(); }
        cancel_losers();
        st->completed = true;
    }

    int get_result() override { return st->winner; }

    void on_won(int index) override {
        st->winner = index;
        if(st->parked && !st->completed) {
            st->completed = true;
            this->resume(nullptr);
        }
    }

private:
    void cancel_losers() {
        int w = st->winner;

        for(auto& c : st->cases) {
            if(c->index() == w) { continue; }
            c->cancel();
        }
    }
};

} // detail

/**
 @brief register with multiple channels for send or receive

 Example:
 ```
 using hce;

 co<bool> foo(channel::unbuffered<int> ich, channel::unbuffered<std::string> sch)
     channel::select select;
     int i;
     std::string s = "hello";

     select.recv(ich, i, [&]{ 
         std::cout << "received int: " << i << std::endl;
     });

     select.send(sch, s, [&]{ 
         std::cout << "sent std::string: " << s << std::endl;
     });

     if(select.call(co_await select.wait())) {
        // someone succeeded
        co_return true;
     } else {
        // we were all canceled
        co_return false;
     }
 }
 ```
 */
struct select {
    struct callback {
        virtual ~callback(){}
        virtual void call() = 0;
    };

    template <typename T>
    int recv(unbuffered<T>& ch, T& dest) {
        return append(pair{ 
            std::make_unique<recv_case<T>>(ch, dest, group, idx)
        });
    }

    template <typename T, typename CALLBACK>
    int recv(unbuffered<T>& ch, T& dest, CALLBACK&& cb) {
        return append(pair{ 
            std::make_unique<recv_case<T>>(ch, dest, group, idx),
            callback_imp<CALLBACK>::make(std::forward<CALLBACK>(cb))
        });
    }

    template <typename T>
    int send(unbuffered<T>& ch, T& src) {
        return append(pair{ 
            std::make_unique<send_case<T>>(ch, src, group, idx)
        });
    }

    template <typename T, typename CALLBACK>
    int send(unbuffered<T>& ch, T& src, CALLBACK&& cb) {
        return append(pair{ 
            std::make_unique<send_case<T>>(ch, src, group, idx),
            callback_imp<CALLBACK>::make(std::forward<CALLBACK>(cb))
        });
    }

    // Returns an awaitable that parks until exactly one case wins.
    // The select object must remain alive until the awaitable completes.
    // Result is the winning case index, or -1 if every case failed.
    inline hce::awt<int> wait() {
        auto state = std::make_unique<select_state>();
        state->group = &group;
        state->cases = std::move(cases);
        return hce::awt<int>(new detail::select_impl(std::move(state)));
    }

    /// call the selected callback
    inline bool call(int idx) { 
        if(idx > -1 && cases[idx].second) {
            (*(cases[idx].second))(); 
            return true;
        } else {
            return false;
        }
    }

private:
    inline int append(pair&& p) {
        int idx = static_cast<int>(cases.size());
        cases.push_back(std::move(p));
        return idx;
    }

    template <typename CALLBACK>
    struct callback_impl {
        static callback_impl<CALLBACK>* make(const CALLBACK& c) { 
            return new callback_impl<CALLBACK>( c );
        }

        static callback_impl<CALLBACK>* make(CALLBACK&& c) { 
            return new callback_impl<CALLBACK>( std::move(c) );
        }

        ~callback_impl(){}
        inline void call() { cb(); }

        CALLBACK cb;
    };

    struct pair {
        std::unique_ptr<case_base> first;
        std::unique_ptr<callback> second;
    };

    select_group group;
    std::vector<pair> cases;

};

} // channel
} // hce
