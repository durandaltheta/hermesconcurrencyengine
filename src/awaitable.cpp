//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 

#include <string>
#include <sstream>

#include "hce/logging.hpp"
#include "hce/coroutine.hpp"
#include "hce/awaitable.hpp"
#include "hce/scheduler.hpp"

hce::awaitable::interface::interface() { HCE_LOW_CONSTRUCTOR(); }

hce::awaitable::interface::~interface() { 
    HCE_TRACE_DESTRUCTOR();

    if(this->handle_) [[unlikely]] {
        // place handle in coroutine for printing purposes
        coroutine co(std::move(this->handle_));
        std::stringstream ss;
        ss << *this
           << " was not resumed before being destroyed; it held " 
           << co;
        HCE_FATAL_METHOD_BODY("~interface",ss.str().c_str());

        // Can't recover anyway, about to terminate. Leaving the handle 
        // in this destructor and cleaning up would cause a circular 
        // destructor call (IE, an object inside the coroutine would 
        // destruct the coroutine containing it), causing a very 
        // confusing error.
        co.release();
        std::terminate();
    }
}

void* hce::awaitable::interface::operator new(std::size_t n) noexcept {
    return hce::memory::allocate(n);
}

void hce::awaitable::interface::operator delete(void* ptr) noexcept {
    hce::memory::deallocate(ptr);
}

std::string hce::awaitable::interface::info_name() { 
    return "hce::awaitable::interface"; 
}

std::string hce::awaitable::interface::name() const { 
    return interface::info_name(); 
}

bool hce::awaitable::interface::awaited() { return this->awaited_; }

bool hce::awaitable::interface::await_ready() {
    HCE_LOW_METHOD_ENTER("await_ready");

    // acquire the lock if implementation was not constructed with 
    // ownership
    if(this->await_policy() == await::policy::defer) { this->lock(); }

    // set awaited flag
    this->awaited_ = true;

    // call the ready code
    if(this->on_ready()) [[unlikely]] {
        HCE_TRACE_METHOD_BODY("await_ready","ready immediately");
        this->unlock();
        return true;
    } else [[likely]] {
        HCE_TRACE_METHOD_BODY("await_ready","about to suspend");
        return false;
    }
}

void hce::awaitable::interface::await_suspend(std::coroutine_handle<> h) {
    HCE_LOW_METHOD_ENTER("await_suspend");

    // still locked from await_ready()
    this->on_suspend();

    // If this handle is valid, we are in a coroutine. Optimize for 
    // coroutine suspends over system threads
    if(h) [[likely]] {
        HCE_TRACE_METHOD_BODY("await_suspend",h);

        // assign the handle to our member
        this->handle_ = h; 

        // the current coroutine no longer manages the handle
        coroutine::local().release(); 

        // Compiler now returns to the caller of coroutine::resume() 
        // when this function returns
    } else [[unlikely]] {
        // Behavior of system thread in this function is VERY different 
        // than in coroutines. We block here on a condition_variable 
        // until resume() is called, where-as in a coroutine it takes
        // control of the coroutine handle and suspends.

        // block the calling thread using traditional mechanisms
        this->tt_ = detail::coroutine::this_thread::get(); 

        HCE_TRACE_METHOD_BODY("await_suspend","tt_:",(void*)(this->tt_));

        // allow condition_variable::wait() to unlock `this`
        detail::coroutine::this_thread::block(*this);

        // we are now re-locked and resumed
    }

    // in both cases we need to exit this function unlocked
    this->unlock();
}

void hce::awaitable::interface::resume(void* m) {
    HCE_LOW_METHOD_ENTER("resume");

    auto rp = this->resume_policy();

    // acquire the lock
    if(rp == resume::policy::lock){ this->lock(); }

    // call the custom resumption code
    this->on_resume(m); 

    // optimize for coroutines over threads
    if(this->handle_) [[likely]] { 
        HCE_TRACE_METHOD_BODY("resume","to_destination");
        // unblock the suspended coroutine and push the handle to its 
        // destination. Make sure that handle_ is unset before passing 
        // to destination. There are certain cases where the the 
        // coroutine can be rescheduled where this can cause an error 
        // otherwise in "no-lock" scenarios.
        auto h = this->handle_;
        this->handle_ = std::coroutine_handle<>();
        if(rp != resume::policy::no_lock) { this->unlock(); }
        this->to_destination(h);
    } else [[unlikely]] {
        if(this->tt_) [[unlikely]] { 
            HCE_TRACE_METHOD_BODY("resume","unblock");
            // unblock the suspended thread 
            auto tt = this->tt_;
            this->tt_ = nullptr;
            if(rp == resume::policy::no_lock) { tt->unblock(); }
            else { tt->unblock(*this); }
        } else [[likely]] {
            HCE_TRACE_METHOD_BODY("resume","not blocked");
            // this was called before blocking occurred
            if(rp != resume::policy::no_lock) { this->unlock(); }
        }
    }
}

void hce::awaitable::interface::on_suspend() {
    auto& tl_sch = detail::scheduler::tl_this_scheduler();

    /* 
     Now that we're suspending we know to try and acquire a 
     destination. If we're not in a scheduler then we're going to block 
     on the thread_local condition variable and destination() won't be 
     called.
     */
    if(tl_sch) {
        this->destination_ = (void*)tl_sch;
    }
}

/// pass a resumed coroutine to its destination
void hce::awaitable::interface::to_destination(std::coroutine_handle<> h) {
    ((hce::scheduler*)destination_)->schedule_(std::move(h));
}

hce::awaitable::awaitable() : impl_(nullptr) {
    HCE_TRACE_CONSTRUCTOR();
}

hce::awaitable::~awaitable() { 
    HCE_TRACE_DESTRUCTOR();
    wait(); 
}

std::string hce::awaitable::info_name() { return "hce::awaitable"; }
std::string hce::awaitable::name() const { return awaitable::info_name(); }

std::string hce::awaitable::content() const {
    return impl_ ? impl_->to_string() : std::string();
}

bool hce::awaitable::valid() const { return (bool)impl_; }

hce::awaitable::interface& hce::awaitable::implementation() { return *impl_; }

void hce::awaitable::reset() { impl_.reset(); }

void hce::awaitable::reset(interface* i) { impl_.reset(i); }

hce::awaitable::interface* hce::awaitable::release() { return impl_.release(); }

bool hce::awaitable::await_ready() { return impl_->await_ready(); }

void hce::awaitable::await_suspend(std::coroutine_handle<> h) { 
    impl_->await_suspend(h); 
}

void hce::awaitable::wait() {
    HCE_MIN_METHOD_ENTER("wait");
    if(impl_ && !(impl_->awaited())) [[unlikely]] {
        if(coroutine::in()) [[unlikely]] { 
            // coroutine failed to `co_await` the awaitable
            std::stringstream ss;
            ss << hce::coroutine::local()
               << "did not call co_await on "
               << *this;
            HCE_FATAL_METHOD_BODY("wait",ss.str());
            std::terminate();
        } else if(!await_ready()) [[likely]] { 
            HCE_TRACE_METHOD_BODY("wait","thread");
            // if we're here, this awaitable is operating without the 
            // `co_await` keyword, and needs to operate as a regular system 
            // thread blocking call, not a coroutine suspend.
            await_suspend(std::coroutine_handle<>()); 
        } else {
            HCE_TRACE_METHOD_BODY("wait","thread done");
        }
    } else {
        HCE_TRACE_METHOD_BODY("wait","nothing to do");
    }
}

void hce::awaitable::interface_deleter::operator()(interface* ptr) const noexcept {
    ptr->~interface(); // call destructor
    ptr->operator delete(ptr);  // call custom delete 
}
