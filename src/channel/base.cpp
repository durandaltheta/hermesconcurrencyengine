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
    std::lock_guard<interface> lk(*this);
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
    
bool compare_exchange_strong(bool& expected, bool desired) noexcept {
    if(claimed == expected) {
        claimed = desired;
        return true;
    }

    expected = claimed;
    return false;
}

bool try_claim(group::interface** groups, std::size_t count, int index) {
    bool result = false;
    bool expected = false;
    std::size_t i=0; 

    // filter out non-unique groups, the new sz may be smaller than cout
    auto sz = hce::sort::unique_non_null(groups, count);

    // sort the pointers smallest to largest
    hce::sort::pointers(groups, sz);

    // lock left to right
    for(; i<sz; ++i) {
        groups[i]->lock();
    }

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

namespace group {

impl<hce::spinlock>::~impl(){}
void impl<hce::spinlock>::lock() { lk_.lock(); }
void impl<hce::spinlock>::unlock() { lk_.unlock(); }

} // group

namespace select {
namespace clause {

base::base(channel::interface<T>& c, const channel::select::request& r) :
    ch(&c), 
    request(r)
{ }

void ~base::base() { }

bool base::activate(select::notify::interface* owner) {
    awt = ch->select().request(request);
    dynamic_cast<channel::detail::awt::base>(awt.get())->owner = owner;
    return ch->select().activate(awt.get());
}

void base::cancel() {
    if(ch && awt.valid()){ 
        ch->cancel(awt.get()); 
    }
}

send::send(channel::interface<T>& c, const channel::request& r) :
    base(c, r)
{ }
    
bool send::is_send() const { return true; }

recv::recv(channel::interface<T>& c, const channel::request& r) :
    base(c, r)
{ }
    
bool recv::is_send() const { return false; }

} // clause 
} // select
} // detail
} // channel
} // hce
