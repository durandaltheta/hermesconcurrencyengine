//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#include <mutex>
#include <cstdint>

#include "hce/channel/base.hpp"

namespace hce {
namespace channel {
namespace select {
namespace group {

void interface::~interface() { }

void interface::reset() {
    claimed = false;
    winner = -1;
}

bool interface::is_claimed() const {
    std::lock_guard<interface> lk(*this);
    return claimed;
}

int interface::winner_index() const {
    std::lock_guard<interface> lk(*this);
    return winner;
}
    
bool try_claim(group** groups, std::size_t count, int index) {
    bool result = false;
    std::size_t i=0;

    // lock left to right
    for(; i<count; ++i) {
        groups[i]->lock();
    }

    bool expected = false;

    // attempt to claim 
    if(compare_exchange_strong(expected, true) {
        winner = index;
        result = true;
    }

    // unlock right to left
    for(; i>=0; --i) {
        groups[i]->unlock();
    }

    return result;
}

bool compare_exchange_strong(bool& expected, bool desired) noexcept {
    if(claimed == expected) {
        claimed = desired;
        return true;
    }

    expected = claimed;
    return false;
}

} // group
} // select

namespace detail {

namespace awt {

base::base(const channel::request& r) :
    request(r) 
{ }

base::~base() { }

namespace send {

base::base(const channel::request& r) :
    awt::base(r),
    r.move ? detail::transfer(detail::pointer_send<T&&>, r.value)
           : detail::transfer(detail::pointer_send<const T&>, r.value)
{ }

base::~base() { }

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

base::base(const channel::request& r) :
    awt::base(r),
    destination(r.value)
{ }

base::~base() { }

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
} // awt

namespace select {
namespace clause {

partial::partial(channel::interface<T>& c, const channel::select::request& r) :
    ch(&c), 
    request(r),
    iface(ch.select().request(r))
{
}

void ~partial::partial() {
    if(iface) {
        // if this is a non-coroutine, it will block, otherwise it will become 
        // a fatal error
        hce::awt<bool>(iface);
    }
}

void partial::cancel() {
    if(iface && ch){ 
        ch->cancel(iface); 
        iface = nullptr;
    }
}
    
hce::awt<bool> complete() {
    auto i = iface;
    iface = nullptr;
    return { i };
}

int partial::index() const { return idx; }

send::send(channel::interface<T>& c, const channel::request& r) :
    partial(c, r)
{ }

bool send::activate(select::notify* owner) {
    request.owner = owner;
    op = ch->select().send(request);
    iface = dynamic_cast<hce::awt<bool>::interface*>(op.get());
    return ch->select().activate(iface);
}
    
bool send::is_send() const { return true; }

recv::recv(channel::interface<T>& c, const channel::request& r) :
    partial(c, r)
{ }

bool recv::activate(select::notify* owner) {
    request.owner = owner;
    op = ch->select().recv(request);
    iface = dynamic_cast<hce::awt<bool>::interface*>(op.get());
    return ch->select().activate(iface);
}
    
bool recv::is_send() const { return false; }

} // clause 
} // select
} // detail
} // channel
} // hce
