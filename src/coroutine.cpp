//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 

#include <string>
#include <sstream>

#include "hce/atomic.hpp"
#include "hce/logging.hpp"
#include "hce/thread.hpp"
#include "hce/memory.hpp"
#include "hce/coroutine.hpp"
#include "hce/thread_key_map.hpp"

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
