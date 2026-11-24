#pragma once
//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 

#include "hce/select.hpp"
#include "hce/timer.hpp"

namespace hce { 
namespace detail {
namespace select {

struct awt : hce::awaitable::lockable<hce::spinlock, detail::select::notify> {
    explicit awt(std::unique_ptr<state> s, hce::chrono::duration t):
          hce::awaitable::lockable<hce::spinlock, detail::select::notify>(
              s->lk,
              hce::awaitable::await::policy::defer,
              hce::awaitable::resume::policy::no_lock),
        st(std::move(s)),
        timeout(std::move(t))
    { }

    static std::string info_name() { return "hce::detail::select::awt";  }
    std::string name() const { return awt::info_name(); }

    bool on_ready() {
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

    void on_resume(void*) {
        if(st->winner < 0) { st->winner = st->group->winner_index(); }
        cancel_losers();
        st->completed = true;
    }

    int get_result() { return st->winner; }

    void on_won(int index) {
        st->winner = index;
        if(st->parked && !st->completed) {
            st->completed = true;
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

    std::unique_ptr<state> st;
    hce::chrono::duration timeout;
    hce::sid sid;
    hce::awt<bool> timer;
};

} // select 
} // detail

select& select::clause(std::unique_ptr<channel::select::clause::base> ca) {
    clauses.push_back(std::move(ca));
    return *this;
}

hce::awt<int> select::wait(hce::chrono::duration timeout) {
    struct local {
        static hce::co<void> timer_op(detail::select::awt* sawt, 
                                      hce::awt<bool> timer) 
        {
            if(co_await std::move(timer)) {
                // we timed out, time to cancel the select 
                sawt->cancel_losers();
            }
        }

        static hce::co<bool> main_op(detail::select::awt* sawt) {
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

            // await on a select clause to succeed or all fail
            bool result = co_await hce::awt<bool>(sawt);

            if(timer_awt.valid()) {
                // cancel our running timer
                hce::timer::cancel(sid);
                co_await std::move(timer_awt);
            }
            
            co_return result;
        }
    };

    auto state = std::make_unique<state>();
    state->group = &group;
    state->clauses = &clauses;
    return hce::awt<int>(
        hce::schedule(
            local::op(new detail::select::awt(std::move(state)))));
}
    
bool select::call(int idx) { 
    bool result = false;

    if(idx > -1 && clauses[idx]) {
        ((detail::select::callback*)(clauses[idx].get()))->call();
        result = true;
    } 

    return result;
}

} // hce
