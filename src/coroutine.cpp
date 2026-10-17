//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#include <string>
#include <sstream>
#include <thread>

#include "loguru.hpp"
#include "atomic.hpp"
#include "thread.hpp"
#include "coroutine.hpp"
#include "thread_key_map.hpp"

hce::coroutine*& hce::detail::coroutine::tl_this_coroutine() {
    thread_local hce::thread::local::ptr<hce::thread::key::coroutine> p;
    return p.ref();
}

hce::coroutine::promise_type::promise_type(){}
hce::coroutine::promise_type::~promise_type(){}

hce::coroutine hce::coroutine::promise_type::get_return_object_on_allocation_failure() {
    throw std::bad_alloc();
    return coroutine(nullptr);
}

void* hce::coroutine::promise_type::cleanup_alloc(size_t n) {
    return hce::memory::allocate(n);
}

void hce::coroutine::promise_type::cleanup_dealloc(void* p) {
    hce::memory::deallocate(p);
}

void* hce::coroutine::promise_type::operator new(std::size_t n) noexcept {
    return hce::memory::allocate(n);
}

void hce::coroutine::promise_type::operator delete(void* ptr) noexcept {
    hce::memory::deallocate(ptr);
}

hce::coroutine hce::coroutine::promise_type::get_return_object() {
    return { 
        std::coroutine_handle<promise_type>::from_promise(*this) 
    };
}

std::suspend_always hce::coroutine::promise_type::initial_suspend() { 
    return {}; 
}

std::suspend_always hce::coroutine::promise_type::final_suspend() noexcept { 
    return {}; 
}

void hce::coroutine::promise_type::unhandled_exception() { 
    eptr = std::current_exception(); 
}

hce::coroutine::coroutine() { }

hce::coroutine::coroutine(hce::coroutine&& rhs) {
    HCE_MED_GUARD(rhs.handle_, HCE_MED_CONSTRUCTOR(rhs)); 
    swap(rhs); 
}

// construct the coroutine from a type erased handle
hce::coroutine::coroutine(std::coroutine_handle<>&& h) : handle_(h) { 
    HCE_MIN_GUARD(h,HCE_MIN_CONSTRUCTOR(h));

    // ensure source memory no longer has access to the handle
    h = std::coroutine_handle<>();
}

hce::coroutine& hce::coroutine::operator=(hce::coroutine&& rhs) { 
    HCE_MED_METHOD_ENTER("operator=",rhs);
    swap(rhs);
    return *this;
}

hce::coroutine::~coroutine() {
    HCE_MED_GUARD(handle_,HCE_MED_DESTRUCTOR()); 
    reset(); 
}

std::string hce::coroutine::info_name() { return "hce::coroutine"; }
std::string hce::coroutine::name() const { return coroutine::info_name(); }

std::string hce::coroutine::content() const { 
    if(handle_) {
        std::stringstream ss;
        ss << handle_;
        return ss.str();
    } else { return std::string(); }
}

hce::coroutine::operator bool() const { return (bool)handle_; }

std::coroutine_handle<> hce::coroutine::release() {
    HCE_LOW_METHOD_ENTER("release");
    auto h = handle_;
    handle_ = std::coroutine_handle<>();
    return h;
}

void hce::coroutine::reset() { 
    HCE_TRACE_METHOD_ENTER("reset");
    if(handle_) [[likely]] { destroy_(); }
    handle_ = std::coroutine_handle<>(); 
}

void hce::coroutine::reset(std::coroutine_handle<> h) { 
    HCE_TRACE_METHOD_ENTER("reset", h);
    if(handle_) [[likely]] { destroy_(); }
    handle_ = h; 
}

void hce::coroutine::swap(coroutine& rhs) noexcept { 
    HCE_TRACE_METHOD_ENTER("swap", rhs);
    std::coroutine_handle<> h = handle_;
    handle_ = rhs.handle_;
    rhs.handle_ = h;
}

bool hce::coroutine::done() const { 
    bool d = handle_.done();
    HCE_MIN_METHOD_BODY("done", std::boolalpha, d);
    return d; 
}

void* hce::coroutine::address() const { 
    HCE_MIN_METHOD_ENTER("address");
    return handle_.address(); 
}

bool hce::coroutine::in() { 
    bool ret = detail::coroutine::tl_this_coroutine(); 
    HCE_TRACE_FUNCTION_BODY("hce::coroutine::in():",ret);
    return ret;
}

hce::coroutine& hce::coroutine::local() { 
    HCE_TRACE_FUNCTION_ENTER("hce::coroutine::local()");
    return *(detail::coroutine::tl_this_coroutine()); 
}

void hce::coroutine::resume() {
    HCE_MED_METHOD_ENTER("resume");
    auto& tl_co = detail::coroutine::tl_this_coroutine();

    // store parent coroutine pointer
    auto parent_co = tl_co;

    // set current coroutine ptr 
    tl_co = this; 
    
    // continue coroutine execution
    handle_.resume();

    /*
     Optimize for handle stealing awaitable blocking operations by not 
     expecting this handle to remain valid.
     */
    if(handle_) [[unlikely]] {
        // acquire the exception pointer
        auto eptr = 
            std::coroutine_handle<promise_type>::from_address(address())
                .promise()
                .eptr;

        // rethrow any caught exceptions from the coroutine
        if(eptr) [[unlikely]] { std::rethrow_exception(eptr); }
    }

    // restore the parent pointer
    tl_co = parent_co;
}

void hce::coroutine::destroy_() {
    HCE_MED_METHOD_BODY("destroy", handle_);
    handle_.destroy(); // destruct and deallocate memory 
}

std::string hce::detail::coroutine::this_thread::info_name() { 
    return "hce::detail::coroutine::this_thread"; 
}

std::string hce::detail::coroutine::this_thread::name() const { 
    return hce::detail::coroutine::this_thread::info_name(); 
}

hce::detail::coroutine::this_thread* 
hce::detail::coroutine::this_thread::get() {
    struct init {
        init() {
            if(ptr.ref() == nullptr) {
                obj.reset(new hce::detail::coroutine::this_thread);
                ptr.ref() = obj.get();
            }
        }

        std::unique_ptr<hce::detail::coroutine::this_thread> obj;
        hce::thread::local::ptr<hce::thread::key::coroutine_thread> ptr;
    };

    thread_local init i;
    return i.ptr.ref();
}

void hce::detail::coroutine::this_thread::unblock() {
    HCE_TRACE_METHOD_ENTER("unblock");
    ready = true;
    cv.notify_one();
}

hce::detail::coroutine::this_thread::this_thread() { }


hce::detail::yield::yield() { HCE_LOW_CONSTRUCTOR(); }

hce::detail::yield::~yield() {
    HCE_LOW_DESTRUCTOR();

    if(hce::coroutine::in() && !awaited_){ 
        std::stringstream ss;
        ss << hce::coroutine::local()
           << "did not call co_await on "
           << *this;
        HCE_FATAL_LOG("%s",ss.str().c_str());
        std::terminate();
    }
}

std::string hce::detail::yield::info_name() { 
    return "hce::detail::yield"; 
}

std::string hce::detail::yield::name() const { 
    return yield::info_name(); 
}

bool hce::detail::yield::await_ready() {
    HCE_LOW_METHOD_ENTER("await_ready");
    awaited_ = true;
    return false; 
}

// don't need to replace the handle, it's not moving
void hce::detail::yield::await_suspend(std::coroutine_handle<> h) { 
    HCE_LOW_METHOD_ENTER("await_suspend");
}

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
