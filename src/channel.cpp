//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
// c++
#include <utility>
#include <iterator>
#include <mutex>

#include "hce/channel.hpp"
#include "hce/fatal.hpp"

namespace hce {

void sync_t::participant_t::release() {
    // release all the guards in the predetermined set order
    for(auto& guard : guards_) {
        guard->release();
    }
}

void sync_t::participant_t::insert(channel::guard_t* g) {
    std::lock_guard<std::mutex> lk(mtx_);
    guards_.insert(g);
}

void sync_t::participant_t::erase(channel::guard_t* g) {
    std::lock_guard<std::mutex> lk(mtx_);
    guards_.erase(g);
}

bool sync_t::participant_t::try_claim_recv() {
    // iterate over all guards in the predetermined set order
    auto it = guards_.begin();

    for(; it != guards_.end(); ++it) {
        if(!((*it)->try_claim_recv())) {
            // `it` points at the failing clause, roll back from the previous 
            if(it != guards_.begin()) {
                --it;// last successful
                for(;; --it) {
                    (*it)->release();
                    if(it == guards_.begin()) { break; }
                }
            }

            return false;
        }
    }

    return true;  // everything succeeded
}
        
void sync_t::participant_t::next() {
    auto end = guards_.end();

    if(current_ != end) {
        current_ = std::next(current_);

        if(current_ == end) {
            current_ = guards_.begin();
        }
    }
}

multiplex::interface::~interface() { }

multiplex::erased::erased(
        multiplex::interface* i, 
        multiplex::interface::deleter d) :
    interface(i),
    deleter(d)
{ }
        
multiplex::erased::erased(multiplex::erased&& rhs) {
    interface = rhs.interface;
    deleter = rhs.deleter;
    rhs.interface = nullptr;
    rhs.deleter = nullptr;
}

multiplex::erased::~erased() {
    if(deleter) {
        deleter(interface);
        deleter = nullptr;
        interface = nullptr;
    }
}
        
multiplex::erased& multiplex::erased::operator=(multiplex::erased&& rhs) {
    std::swap(interface, rhs.interface);
    std::swap(deleter, rhs.deleter);
    return *this;
}

multiplex::guard_mgr::guard_mgr(channel::guard&& g) :
    guard(std::move(g))
{
}

bool multiplex::participant::finished() {
    return finished_.load(std::memory_order_acquire);
}

void multiplex::participant::finish(bool val) {
    finished_.store(val, std::memory_order_release); 
}

multiplex::~multiplex() {
    if(participant_) {
        hce::fatal(
           "did not call hce::multiplex::finish() on ",
           *this);
    }
}

std::string multiplex::info_name() { 
    return "hce::multiplex"; 
}

std::string multiplex::name() const { 
    return multiplex::info_name(); 
}

hce::awt<bool> multiplex::select() {
    struct local {
        hce::co<bool> op(multiplex* self) {
            result_t result;

            if(co_await self->ready_.recv(result)) {
                auto& intf = *(self->clauses_[result.idx].e.interface);

                if(result.done) {
                    if(intf.awt.valid()) {
                        // join the finished coroutine
                        co_await std::move(intf.awt);
                    }
                } else {
                    // execute the callback for the associated clause
                    intf.call();
                }

                // release the claim now that callback handling is complete
                self->participant_->release();
                co_return true;
            } else {
                // selection failed, the ready channel is closed
                co_return false;
            }
        }
    };

    return hce::schedule(local::op(this));
}

hce::awt<void> multiplex::finish() {
    struct local {
        static hce::co<void> op(multiplex* self) {
            // close the ready channel
            self->ready_.close();

            // remove the guard from the channels and end forwarding operations
            for(size_t idx=0; idx < self->clauses_.size(); ++idx) {
                auto& intf = *(self->clauses_[idx].e.interface);

                if(intf.awt.valid()) {
                    // join with the forwarding coroutine
                    co_await std::move(intf.awt);
                }
            }

            // clean everything up
            self->clauses_.clear();
            self->participant_.reset();
        }
    };

    return hce::schedule(local::op(this));
}

void multiplex::launch_(std::vector<channel::base_interface*>& bis) {
    participant_.reset(new multiplex::participant(std::move(bis)));

    for(const auto& e : clauses_) {
        e.e.interface->launch();
    }
}

};
