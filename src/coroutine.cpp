//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#include <string>
#include <sstream>
#include <thread>
#include <exception>

#include "loguru.hpp"
#include "atomic.hpp"
#include "coroutine.hpp"

static thread_local hce::coroutine* tl_hce_this_coroutine = nullptr;
static thread_local std::exception_ptr tl_hce_this_exception = std::exception_ptr();

void hce::coroutine::promise_type::unhandled_exception() {
    tl_hce_this_exception = std::current_exception(); 
}

hce::coroutine::coroutine() { }

hce::coroutine::coroutine(coroutine&& rhs) {
    HCE_MED_GUARD(rhs.handle_, HCE_MED_CONSTRUCTOR(rhs)); 
    swap(rhs); 
}

// construct the coroutine from a type erased handle
hce::coroutine::coroutine(std::coroutine_handle<>&& h) : handle_(h) { 
    HCE_MIN_GUARD(h,HCE_MIN_CONSTRUCTOR(h));

    // ensure source memory no longer has access to the handle
    h = std::coroutine_handle<>();
}

hce::coroutine& hce::coroutine::operator=(coroutine&& rhs) { 
    HCE_MED_METHOD_ENTER("operator=",rhs);
    swap(rhs);
    return *this;
}

hce::coroutine::~coroutine() {
    HCE_MED_GUARD(handle_,HCE_MED_DESTRUCTOR()); 
    reset(); 
}

std::string hce::coroutine::info_name() { return "hce::coroutine"; }
std::string hce::coroutine::name() const { return hce::coroutine::info_name(); }

/// return our stringified coroutine handle's address
std::string hce::coroutine::content() const { 
    if(handle_) {
        std::stringstream ss;
        ss << handle_;
        return ss.str();
    } else { return std::string(); }
}

/// return true if the handle is valid, else false
hce::coroutine::operator bool() const { return (bool)handle_; }

/// releases ownership of the managed handle and returns it
std::coroutine_handle<> hce::coroutine::release() {
    HCE_LOW_METHOD_ENTER("release");
    auto h = handle_;
    handle_ = std::coroutine_handle<>();
    return h;
}

/// cleans up and resets the managed handle
void hce::coroutine::reset() { 
    HCE_TRACE_METHOD_ENTER("reset");
    if(handle_) [[likely]] { destroy_(); }
    handle_ = std::coroutine_handle<>(); 
}

/// cleans up and replaces the managed handle
void hce::coroutine::reset(std::coroutine_handle<> h) { 
    HCE_TRACE_METHOD_ENTER("reset", h);
    if(handle_) [[likely]] { destroy_(); }
    handle_ = h; 
}

/// swap two coroutines
void hce::coroutine::swap(hce::coroutine& rhs) noexcept { 
    HCE_TRACE_METHOD_ENTER("swap", rhs);
    std::coroutine_handle<> h = handle_;
    handle_ = rhs.handle_;
    rhs.handle_ = h;
}

/// return true if the coroutine is done, else false
bool hce::coroutine::done() const { 
    bool d = handle_.done();
    HCE_MIN_METHOD_BODY("done", std::boolalpha, d);
    return d; 
}

/// return the address of the underlying handle
void* hce::coroutine::address() const { 
    HCE_MIN_METHOD_ENTER("address");
    return handle_.address(); 
}

/// return true if called inside a running coroutine, else false
bool hce::coroutine::in() { 
    bool ret = tl_hce_this_coroutine;
    HCE_TRACE_FUNCTION_BODY("hce::coroutine::in():",ret);
    return ret;
}

/// return the coroutine running on this thread
hce::coroutine& hce::coroutine::local() { 
    HCE_TRACE_FUNCTION_ENTER("hce::coroutine::local()");
    return *(tl_hce_this_coroutine); 
}

/// resume the coroutine 
void hce::coroutine::resume() {
    HCE_MED_METHOD_ENTER("resume");
    auto& tl_co = tl_hce_this_coroutine;

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
        // rethrow any caught exceptions from the coroutine
        if(tl_hce_this_exception) [[unlikely]] { 
            auto eptr = tl_hce_this_exception;
            // reset the exception_ptr thread_local memory
            tl_hce_this_exception = std::exception_ptr();
            std::rethrow_exception(eptr); 
        }
    }

    // restore the parent pointer
    tl_co = parent_co;
}

void hce::coroutine::destroy_() {
    HCE_MED_METHOD_BODY("destroy", handle_);
    handle_.destroy(); // destruct and deallocate memory 
}
