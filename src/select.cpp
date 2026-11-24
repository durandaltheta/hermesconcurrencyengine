#pragma once
//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 

#include "hce/select.hpp"
#include "hce/timer.hpp"

namespace hce { 
namespace detail {
namespace select {

struct awt : hce::awaitable::lockable<hce::spinlock, detail::select::notify> {
    explicit awt(select& p, std::unique_ptr<state> s, hce::chrono::duration t):
        hce::awaitable::lockable<hce::spinlock, detail::select::notify>(
            s->lk,
            hce::awaitable::await::policy::defer,
            hce::awaitable::resume::policy::no_lock),
        parent(p),
        st(std::move(s)),
        timeout(std::move(t))
    { }

    static std::string info_name() { return "hce::detail::select::awt";  }
    std::string name() const { return awt::info_name(); }

    bool on_ready() {
        int parked_count = 0;

        for(auto& c : *st->clauses) {
            if(c->activate(this)) { ++parked_count; }
        }

        st->parked = true;

        if(st->group->is_claimed()) { // some clause won during activate
            st->winner = st->group->winner_index();
            cancel_losers();
            st->completed = true;
            return true;
        }

        if (parked_count == 0) { // every clause failed/closed
            st->winner = -1;
            st->completed = true;
            return true;
        }

        return false; // waiting
    }

    void on_resume(void*) {
        if(st->winner < 0) { st->winner = st->group->winner_index(); }
        cancel_losers();
        st->completed = true;
    }

    hce::select::result get_result() { return { st->winner }; }

    void on_won(int index) {
        st->winner = index;

        if(st->parked && !st->completed) {
            st->completed = true;

            if(index > -1 && clauses[idx]) {
                // execute the winning callback
                parent.clauses[index]->call();
            }

            // resume the blocking coroutine
            this->resume(nullptr);
        }
    }

    void cancel_losers() {
        int w = st->winner;

        for(auto& c : *(st->clauses)) {
            if(c->index() == w) { continue; }
            c->cancel();
        }
    }

    select& parent;
    std::unique_ptr<state> st;
    hce::chrono::duration timeout;
    hce::sid sid;
    hce::awt<bool> timer;
};

} // select 
} // detail


bool select::result::operator bool() { return index >= 0; }

select::result::error_e select::result::status() { 
    return index < 0 ? (result::error_e)index : result::error_e::ok;
}

int select::clause(std::unique_ptr<channel::select::clause::base> cb) {
    int idx = static_cast<int>(clauses.size());
    clauses.push_back(std::move(ca));
    return idx;
}

hce::awt<hce::select::result> select::wait(hce::chrono::duration timeout) {
    struct local {
        static hce::co<void> timer_op(detail::select::awt* sawt, 
                                      hce::awt<bool> timer) 
        {
            if(co_await std::move(timer)) {
                // we timed out, try to claim the group 
                if(sawt->group->try_claim((int)(select::result::error_e::timeout))) {
                    sawt->cancel_losers();
                }
            }
        }

        static hce::co<bool> op(detail::select::awt* sawt) {
            // sid of the running timer
            hce::sid sid;

            // awaitable of the running timer
            hc::awt<bool> timer_awt;

            // the awaitable for the coroutine waiting on the timer
            hce::awt<void> timer_co_awt;

            if(sawt->timeout != hce::chrono::duration()) {
                // we have a valid timeout, start the timer and its coroutine
                timer_awt = hce::timer::start(sid, sawt->timeout);
                timer_co_awt = hce::schedule(timer_op(sawt, &timer_awt));
            }

            // await till on_won() or everything is canceled
            bool result = co_await hce::awt<bool>(sawt);

            if(timer_awt.valid()) {
                // cancel our running timer
                hce::timer::cancel(sid);
                co_await std::move(timer_awt);
            }

            // cleanup all our memory
            for(auto& c : sawt->parent.clauses) {
                auto partial = (channel::detail::select::clause::partial*)(c.get());
                // properly deallocate internal awaitable
                co_await partial->complete();
            }
   
            parent.clauses.clear();
            co_return result;
        }
    };

    group.reset();
    auto state = std::make_unique<state>();
    state->group = &group;
    state->clauses = &clauses;
    return hce::awt<hce::select::result>(
        hce::schedule(
            local::op(new detail::select::awt(
                *this,
                std::move(state),
                std::move(timeout)))));
}

void select::cancel(const hce::awt<int>& awt) {
    auto intf = (detail::select::awt*)(awt.get());

    if(intf->group.try_claim((int)(select::result::error_e::user_cancelled))) {
        intf->cancel_losers();
    }
}

select::no_callback_send::no_callback_send(
        channel::base& ch, 
        const channel::request& r) :
    channel::detail::select::clause::send(ch, r)
{ }

select::no_callback_send::~no_callback_send(){}

void select::no_callback_send::call(){}

select::no_callback_recv::no_callback_recv(
        channel::base& ch, 
        const channel::request& r) :
    channel::detail::select::clause::recv(ch, r)
{ }

select::no_callback_recv::~no_callback_recv(){}

void select::no_callback_recv::call(){}

std::unique_ptr<channel::select::clause::base> make_send(
        channel::base& ch, const channel::request& r)
{
    return { new no_callback_send(ch, r) };
}

std::unique_ptr<channel::select::clause::base> select::make_recv(
            channel::base& ch, const channel::request& r)
{
    return { new no_callback_recv(ch, r) };
}

} // hce
