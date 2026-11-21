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

void multiplex::guard::unblock() {
    // unblock in a random order 
    for(const auto idx : randomized_indexes()) {
        elements[idx].unblock(child.ctx);
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
            auto& mtx = self->guard_mgr_.mtx;

            // mark the guard operations as finished
            co_await hce::mutex::lock(mtx);
            self->guard_mgr_.finished = true;
            mtx.unlock();
   
            // close the received channel
            self->received_.close();

            // remove the guard from the channels and end forwarding operations
            for(auto e : self->elements) {
                co_await e.e.interface->set_guard(nullptr);
            }

            // unblock all channels
            self->guard_mgr_.unblock(); 
           
            // join all running coroutines
            for(auto e : self->elements) {
                co_await std::move(e.awt);
            }

            self->elements.clear();
        }
    };

    return hce::schedule(local::op, this);
}

hce::co<void> multiplex::select() {
    hce::schedule(multiplex::selector, this);
}


std::vector<std::size_t> multiplex::randomized_indexes() {
    std::vector<std::size_t> indices(elements.size());
    std::iota(indices.begin(), indices.end(), 0);   // 0, 1, 2, …, n-1

    std::random_device rd;
    std::mt19937 gen(rd());
    std::shuffle(indices.begin(), indices.end(), gen);

    return indices;
}

hce::co<void> multiplex::selector(multiplex* parent) {
    size_t idx = 0;

    if(co_await parent->received_(idx)) {
        elements_[idx].call();

        // join the finished coroutine
        co_await std::move(parent->elements[idx].awt);

        // If we received then the guard acquired the lock on some
        // send operation. Unblock and notify children.
        mtx.unlock();

        // relaunch the finished forwarder 
        parent->elements[idx].launch();

        // unblock the channels
        parent->guard.unblock();
    }
}

void multiplex::launch_() {
    // launch in a random order 
    for(const auto idx : randomized_indexes()) {
        elements[idx].launch();
    }
}

};
