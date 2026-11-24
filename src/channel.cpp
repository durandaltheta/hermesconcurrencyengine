//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
// c++
#include <utility>
#include <iterator>
#include <mutex>

#include "hce/channel.hpp"
#include "hce/fatal.hpp"

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

awt::awt(std::unique_ptr<state> s):
      hce::awaitable::lockable<hce::spinlock, detail::select::notify>(
          s->lk,
          hce::awaitable::await::policy::defer,
          hce::awaitable::resume::policy::no_lock),
    st(std::move(s))
{}

std::string awt::info_name() { return "hce::channel::detail::select::awt"; }
std::string awt::name() const { return awt::info_name(); }

bool awt::on_ready() {
    int parked_count = 0;

    for(auto& c : *(st->clauses)) {
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

void awt::on_resume(void*) {
    if(st->winner < 0) { st->winner = st->group->winner_index(); }
    cancel_losers();
    st->completed = true;
}

int awt::get_result() { return st->winner; }

void awt::on_won(int index) {
    st->winner = index;
    if(st->parked && !st->completed) {
        st->completed = true;
        this->resume(nullptr);
    }
}

void awt::cancel_losers() {
    int w = st->winner;

    for(auto& c : *(st->clauses)) {
        if(c->index() == w) { continue; }
        c->cancel();
    }
}

} // select

namespace unbuffered {

channel::operation base::request(const channel::request& r) {
    switch(r.op) {
        case channel::request::op_e::send_deep:
            return { this->send_deep_(r), channel::result::success };
        case channel::request::op_e::send_shallow:
            return { this->send_shallow_(r), channel::result::success };
        case channel::request::op_e::try_send_deep:
            return { nullptr, this->try_send_deep_(r) };
        case channel::request::op_e::try_send_deep:
            return { nullptr, this->try_send_shallow_(r) };
        case channel::request::op_e::recv:
            return { this->recv_(r), channel::result::success };
        case channel::request::op_e::try_recv:
            return { nullptr, this->try_recv_(r) };
        default:
            return { nullptr, channel::operation::failure };
    }
}

} // unbuffered
} // detail
} // channel
} // hce
