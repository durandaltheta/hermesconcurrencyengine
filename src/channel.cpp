//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
// c++
#include <utility>

#include "channel.hpp"
#include "synchronization.hpp"

namespace hce {

erased::erased(multiplex::interface* i, multiplex::interface::deleter d) :
    interface(i),
    deleter(d)
{
}
        
erased::erased(erased&& rhs) {
    interface = rhs.interface;
    deleter = rhs.deleter;
    rhs.interface = nullptr;
    rhs.deleter = nullptr;
}

erased::~erased() {
    if(deleter) {
        deleter(interface);
        deleter = nullptr;
        interface = nullptr;
    }
}
        
erased& erased::operator=(erased&& rhs) {
    std::swap(interface, rhs.interface);
    std::swap(deleter, rhs.deleter);
}

bool multiplex::guard_mgr::allow_send() {
    if(mtx.try_lock()) {
        if(finished) {
            mtx.unlock();
            return false;
        } else {
            return true;
        }
    } else {
        return false;
    }
}

void multiplex::guard_mgr::unblock() {
    // unblock in a random order 
    for(const auto idx : randomized_indexes_(guard.children.size())) {
        auto& child = guard.children[idx];

        // only unblock if we're still managing this
        if(child.ctx != nullptr) {
            child.unblock(child.ctx);
        }
    }
}

multiplex::~multiplex() {
    if(guard_mgr_.close == false) {
        std::stringstream ss;
        ss << "did not call hce::multiplex::finish() on "
           << *this;
        HCE_FATAL_LOG("%s",ss.str().c_str());
    }
}

std::string multiplex::info_name() { 
    return type::templatize<T>("hce::multiplex"); 
}

std::string multiplex::name() const { 
    return multiplex::::info_name(); 
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

                if(i.awt.valid()) {
                    // join with the forwarding coroutine
                    co_await std::move(i.awt);
                }
            }

            // clean everything up
            self->guard_mgr_.release();
            self->guard_mgr_.guard.children.clear();
            self->elements_.clear();
        }
    };

    return hce::schedule(local::op, this);
}

hce::co<bool> multiplex::select() {
    return hce::schedule(multiplex::selector, this);
}


std::vector<std::size_t> multiplex::randomized_indexes_(size_t sz) {
    std::vector<std::size_t> indices(sz);
    std::iota(indices.begin(), indices.end(), 0);   // 0, 1, 2, …, n-1

    std::random_device rd;
    std::mt19937 gen(rd());
    std::shuffle(indices.begin(), indices.end(), gen);

    return indices;
}

hce::co<bool> multiplex::selector_(multiplex* parent) {
    auto& p = *parent;
    size_t idx = 0;

    if(co_await p.received_(idx)) {

        // execute the callback for the associated element
        p.elements_[idx].call();

        // join the finished coroutine
        if(co_await std::move(p.elements_[idx].awt)) {
            // relaunch the finished forwarder if we can still do work
            p.elements[idx].launch();
        } else {
            // the operation failed so we need to release control of the channel 
            p.unmanage_(idx);
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
    // launch in a random order 
    for(const auto idx : randomized_indexes_(elements.size())) {
        elements[idx].launch();
    }
}
    
void multiplex::unmanage_(size_t idx) {
    // unmanage a channel and let it operate independently
    elements_[idx].e.interface->set_guard(nullptr);
    auto& child = guard_mgr_.guard.children[idx];
    child.unblock(child.context);
}

};
