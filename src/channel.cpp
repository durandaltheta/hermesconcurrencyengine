//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
// c++
#include "hce/fatal.hpp"
#include "hce/channel.hpp"
#include "hce/channel/unbuffered.hpp"

namespace hce {
namespace channel {
namespace select {

bool group::try_claim(int index) {
    bool expected = false;
    if(claimed.compare_exchange_strong(
            expected, true,
            std::memory_order_acq_rel,
            std::memory_order_acquire)) {
        winner.store(index, std::memory_order_release);
        return true;
    }

    return false;
}

bool group::is_claimed() const {
    return claimed.load(std::memory_order_acquire);
}

int group::winner_index() const {
    return winner.load(std::memory_order_acquire);
}

} // select

namespace detail {
namespace send {

base::base(transfer t, select::group* g, int idx, select::notify* own = nullptr) :
    tx(t),
    group(g),
    index(idx),
    owner(own)
{ }

bool base::try_claim() { return group ? group->try_claim(index) : true; }

void base::on_resume(void* m) {
    if(m) {
        tx.send(static_cast<void*>(m));
        success = true;
        if(owner){ owner->on_won(index); }
    }
}

bool base::get_result() { return success; }

} // send

namespace recv {

base::base(void* d, select::group* g, int idx, select::notify* own = nullptr) :
    destination(d),
    group(g),
    index(idx),
    owner(own)
{ }

bool base::try_claim() { return group ? group->try_claim(index) : true; }

void base::on_resume(void* m) {
    if(m) {
        static_cast<transfer*>(m)->send(destination);
        success = true;
        if(owner){ owner->on_won(index); }
    }
}

bool base::get_result() { return success; }

} // recv

namespace select {
namespace clause {

impl::impl(channel::interface<T>& c, const channel::select::request r&, int idx) :
    ch(&c), 
    request(r),
    index(idx)
{}

void impl::cancel() {
    if(iface && ch){ 
        ch->cancel(iface); 
        iface = nullptr;
    }
}

int impl::index() const { return idx; }

bool send::activate(select::notify* owner) {
    request.owner = owner;
    op = ch->select().send(request);
    iface = dynamic_cast<hce::awt<bool>::interface*>(&op.implementation());
    return ch->select().activate(iface);
}

bool recv::activate(select::notify* owner) {
    request.owner = owner;
    op = ch->select().recv(request);
    iface = dynamic_cast<hce::awt<bool>::interface*>(&op.implementation());
    return ch->select().activate(iface);
}

} // clause 
} // select

namespace unbuffered {

base::base() : selector_(*this), locker_{*this} { }

channel::operation base::request(const channel::request& r) {
    switch(r.op) {
        case channel::request::op_e::send_deep:
            return { this->get_send_interface(r), channel::result::success };
        case channel::request::op_e::send_shallow:
            return { this->get_send_interface(r), channel::result::success };
        case channel::request::op_e::try_send_deep:
            return { nullptr, this->try_send_(r) };
        case channel::request::op_e::try_send_shallow:
            return { nullptr, this->try_send_(r) };
        case channel::request::op_e::recv:
            return { this->get_recv_interface(r), channel::result::success };
        case channel::request::op_e::try_recv:
            return { nullptr, this->try_recv_(r) };
        default:
            return { nullptr, channel::operation::failure };
    }
}
    
int base::size() const { return 0; }
int base::used() const { return 0; }

bool base::closed() const {
    std::lock_guard<locker> lk(locker_);
    return closed_flag_;
}

void base::close() {
    std::lock_guard<locker> lk(locker_);
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
    
void base::cancel(hce::awt<bool>::interface* op) {
    if(!op){ return; }

    std::lock_guard<locker> lk(locker_);
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

hce::awt<bool> base::selector::request(const request& r) {
    return hce::awt<bool>(get_send_interface(r));
}

bool base::selector::activate(hce::awt<bool>::interface* op) {
    if(!op) { return false; }

    std::lock_guard<locker> lk(parent_.locker_);
    return !static_cast<hce::awt_interface<bool>*>(op)->on_ready();
}
        
base::selector::selector(interface<T>& p) : parent(p) { }

void base::locker::lock() { parent_.lock(); }
void base::locker::unlock() { parent_.lock(); }
    
channel::result base::try_send_(const channel::request& r) {
    std::lock_guard<locker> lk(locker_);
    if(closed_flag_) { return result::closed; }

    auto tx = get_send_transfer(r);

    while(parked_recv_.size()) {
        auto* rb = static_cast<detail::recv::base*>(parked_recv_.front());
        if(rb->group && rb->group->is_claimed()) {
            parked_recv_.pop();
            rb->resume(nullptr);
            continue;
        }
        if(rb->try_claim()) {
            parked_recv_.pop();
            rb->resume(static_cast<void*>(&tx));
            return result::success;
        }
        parked_recv_.pop();
        rb->resume(nullptr);
    }

    return result::failure;
}

channel::result base::try_recv_(const channel::request& r) { 
    std::lock_guard<locker> lk(locker_);

    if(closed_flag_) { return result::closed; }

    while(parked_send_.size()) {
        auto* sb = static_cast<detail::send::base*>(parked_send_.front());

        if(sb->group && sb->group->is_claimed()) {
            parked_send_.pop();
            sb->resume(nullptr);
            continue;
        }

        if(sb->try_claim()) {
            parked_send_.pop();
            sb->resume(r.value);
            return result::success;
        }

        parked_send_.pop();
        sb->resume(nullptr);
    }

    return result::failure;
}

} // unbuffered
} // detail
} // channel
} // hce
