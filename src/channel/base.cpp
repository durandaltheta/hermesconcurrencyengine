//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#include "hce/channel/base.hpp"

namespace hce {
namespace channel {
namespace select {

void group::reset() {
    claimed = false;
    winner = -1;
}

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

base::base(transfer t, const channel::request& r) :
    tx(t),
    request(r)
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

base::base(void* d, const channel::request& r) :
    destination(d),
    request(r)
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

bool send::activate(select::notify* owner) {
    request.owner = owner;
    op = ch->select().send(request);
    iface = dynamic_cast<hce::awt<bool>::interface*>(op.get());
    return ch->select().activate(iface);
}

bool recv::activate(select::notify* owner) {
    request.owner = owner;
    op = ch->select().recv(request);
    iface = dynamic_cast<hce::awt<bool>::interface*>(op.get());
    return ch->select().activate(iface);
}

} // clause 
} // select
} // detail
} // channel
} // hce
