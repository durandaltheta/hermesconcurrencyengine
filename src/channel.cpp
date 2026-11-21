//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
// c++
#include <utility>

#include "channel.hpp"

namespace { // anonymous

/*
 Deterministic round-robin index generator

 Begin at an arbitrary start index and walk the range [0, max) repeatedly.
 */
struct round_robin {
    explicit round_robin(std::size_t start = 0) noexcept
        : current_(start) {}

    // return the current index, then advance
    inline std::size_t next(std::size_t max) noexcept {
        std::size_t ret = current_ % max;
        current_ = ret + 1;
        return ret;
    }

    // reset the next index that will be returned
    inline void reset(std::size_t start) noexcept {
        current_ = start;
    }

    // peek without advancing
    inline std::size_t peek(std::size_t max) const noexcept {
        return current_ % max;
    }

private:
    std::size_t current_;
};

} // anonymous

namespace hce {

multiplex::erased::erased(multiplex::interface* i, multiplex::interface::deleter d) :
    interface(i),
    deleter(d)
{
}
        
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

void multiplex::guard_mgr::unblock() {
    round_robin rr(current_idx);

    for(std::size_t n = 0; n < guard.children.size(); ++n) {
        std::size_t idx = rr.next(guard.children.size());
        auto& child = guard.children[idx];

        if(child.ctx) {
            child.unblock(child.ctx);
        }
    }

    // update the starting index for next time after completing the loop
    current_idx = rr.next(guard.children.size());
}

bool multiplex::guard_mgr::try_claim() {
    // pure try, does not hold anything across the call
    if(finished.load(std::memory_order_acquire)) {
        return false;
    }

    bool expected = false;

    return claimed.compare_exchange_strong(
        expected, true,
        std::memory_order_acq_rel,
        std::memory_order_acquire);
}

void multiplex::guard_mgr::release() {
    claimed.store(false, std::memory_order_release);
}

multiplex::~multiplex() {
    if(guard_mgr_.finished.load(std::memory_order_acquire) == false) {
        std::stringstream ss;
        ss << "did not call hce::multiplex::finish() on "
           << *this;
        HCE_FATAL_LOG("%s",ss.str().c_str());
    }
}

std::string multiplex::info_name() { 
    return "hce::multiplex"; 
}

std::string multiplex::name() const { 
    return multiplex::info_name(); 
}

hce::awt<void> multiplex::finish() {
    struct local {
        static hce::co<void> op(multiplex* self) {
            // mark the guard operations as finished
            self->guard_mgr_.finished.store(true, std::memory_order_release);
   
            // close the received channel
            self->received_.close();

            // remove the guard from the channels and end forwarding operations
            for(size_t idx; idx < self->elements_.size(); ++idx) {
                self->unmanage_(idx);
                auto& intf = *(self->elements_[idx].e.interface);

                if(intf.awt.valid()) {
                    // join with the forwarding coroutine
                    co_await std::move(intf.awt);
                }
            }

            // clean everything up
            self->guard_mgr_.release();
            self->guard_mgr_.guard.children.clear();
            self->elements_.clear();
        }
    };

    return hce::schedule(local::op(this));
}

hce::awt<bool> multiplex::select() {
    return hce::schedule(multiplex::selector_(this));
}

hce::co<bool> multiplex::selector_(multiplex* parent) {
    auto& p = *parent;
    result_t result;

    if(co_await p.received_.recv(result)) {
        auto& intf = *(p.elements_[result.idx].e.interface);

        if(result.done) {
            if(intf.awt.valid()) {
                // join the finished coroutine
                co_await std::move(intf.awt);

                // the operation ended, release control of the channel 
                p.unmanage_(result.idx);
            }
        } else {
            // execute the callback for the associated element
            intf.call();
        }

        // release the claim now that callback handling is complete
        p.guard_mgr_.release();

        // unblock the children and allow them to re-try claiming
        p.guard_mgr_.unblock();
        co_return true;
    } else {
        co_return false;
    }
}

void multiplex::launch_() {
    for(const auto& e : elements_) {
        e.e.interface->launch();
    }

    guard_mgr_.finished.store(false, std::memory_order_release);
}
    
void multiplex::unmanage_(size_t idx) {
    // unmanage a channel and let it operate independently
    elements_[idx].e.interface->set_guard(nullptr);
    auto& child = guard_mgr_.guard.children[idx];
    child.unblock(child.ctx);
}

};
