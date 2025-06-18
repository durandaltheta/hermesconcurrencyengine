#include "awaitable.hpp"

std::string hce::detail::awaitable::this_thread::info_name() { 
    return "hce::detail::awaitable::this_thread"; 
}

std::string hce::detail::awaitable::this_thread::name() const { 
    return hce::detail::awaitable::this_thread::info_name(); 
}

hce::detail::awaitable::this_thread* 
hce::detail::awaitable::this_thread::get() {
    thread_local hce::detail::awaitable::this_thread tt;
    return &tt;
}

hce::detail::awaitable::yield::yield() { HCE_LOW_CONSTRUCTOR(); }

hce::detail::awaitable::yield::~yield() {
    HCE_LOW_DESTRUCTOR();

    if(hce::coroutine::in() && !awaited_) { 
        std::stringstream ss;
        ss << hce::coroutine::local()
           << "did not call co_await on "
           << *this;
        HCE_FATAL_LOG("%s",ss.str().c_str());
        std::terminate();
    }
}

std::string hce::detail::awaitable::yield::info_name() { 
    return "hce::detail::awaitable::yield"; 
}

std::string hce::detail::awaitable::yield::name() const { 
    return hce::detail::awaitable::yield::info_name(); 
}

bool hce::detail::awaitable::yield::await_ready() {
    HCE_LOW_METHOD_ENTER("await_ready");
    awaited_ = true;
    return false; 
}

// don't need to replace the handle, it's not moving
void hce::detail::awaitable::yield::await_suspend(std::coroutine_handle<> h) { 
    HCE_LOW_METHOD_ENTER("await_suspend");
}

hce::awaitable::interface::interface(hce::awaitable::await::policy ap, 
                                     hce::awaitable::resume::policy rp) : 
    /*
     This initializes the state bits. Specifically the strange 
     `((ap == await::policy::adopt) << 3)` statements set the "locked" bit based 
     on if the policy indicates the lock should be treated as locked.
     */
    state_(((ap == await::policy::adopt) << 3) | ap | rp)
{ 
    HCE_LOW_CONSTRUCTOR();
}

hce::awaitable::interface::~interface() { 
    HCE_TRACE_DESTRUCTOR();

    if(has_pointer_() && is_coroutine_()) [[unlikely]] {
        // place handle in coroutine for printing purposes
        hce::coroutine co(std::move(get_data_().handle));
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

std::string hce::awaitable::interface::info_name() { 
    return "hce::awaitable::interface"; 
}

std::string hce::awaitable::interface::name() const { 
    return hce::awaitable::interface::info_name(); 
}

bool hce::awaitable::interface::awaited() { 
    return awaited_();
}

bool hce::awaitable::interface::await_ready() {
    HCE_LOW_METHOD_ENTER("await_ready");

    // acquire the lock if implementation was not constructed with ownership
    if(await_policy() == hce::awaitable::await::policy::defer) { 
        lock_(); 
    }

    // set awaited flag
    awaited_(true);

    // call the ready code
    if(this->on_ready()) [[unlikely]] {
        HCE_TRACE_METHOD_BODY("await_ready","ready immediately");
        unlock_();
        return true;
    } else [[likely]] {
        HCE_TRACE_METHOD_BODY("await_ready","about to suspend");
        return false;
    }
}

/// called by awaitable's await_suspend()
void hce::awaitable::interface::await_suspend(std::coroutine_handle<> h){
    HCE_LOW_METHOD_ENTER("await_suspend");

    // still locked from await_ready()
    this->on_suspend();
    has_pointer_(true);

    // If this handle is valid, we are in a coroutine. Optimize if() check for 
    // coroutine suspends over system threads
    if(h) [[likely]] {
        HCE_TRACE_METHOD_BODY("await_suspend",h);

        // assign the handle to our member
        is_coroutine_(true);

        // placement new initialize data to a coroutine handle
        new ((hce::awaitable::interface::data*)&(data_)) 
        hce::awaitable::interface::data{h};

        // the current coroutine no longer manages the handle
        hce::coroutine::local().release(); 

        // Compiler now returns to the caller of coroutine::resume() 
        // when this function returns
    } else [[unlikely]] {
        // Behavior of system thread in this function is VERY different 
        // than in coroutines. We block here on a condition_variable 
        // until resume() is called, where-as in a coroutine it takes
        // control of the coroutine handle and suspends.

        // block the calling thread using traditional mechanisms
        is_coroutine_(false);

        // placement new initialize data to an awaitable::this_thread pointer
        new ((hce::awaitable::interface::data*)&(data_)) 
        hce::awaitable::interface::data{
            hce::detail::awaitable::this_thread::get()
        };

        HCE_TRACE_METHOD_BODY("await_suspend",
                              "this_thread:",
                              (void*)(get_data_().this_thread));

        // allow condition_variable::wait() to unlock `this`
        hce::detail::awaitable::this_thread::block(*this);

        // we are now re-locked and resumed
    }

    // in both cases we need to exit this function unlocked
    unlock_();
}

void hce::awaitable::interface::clean() {
    // ensure our lock is released when the awaitable instance is cleaned up
    if(locked_()) { 
        unlock_();
    }
}

bool hce::awaitable::interface::ready() const {
    return state_ & hce::awaitable::interface::locked_mask_;
}

void hce::awaitable::interface::ready(bool b) {
    state_ = b 
        ? state_ | hce::awaitable::interface::ready_mask_
        : state_ & ~hce::awaitable::interface::ready_mask_;
}
    
bool hce::awaitable::interface::on_ready() { 
    return ready(); 
}

hce::awaitable::await::policy hce::awaitable::interface::await_policy() const { 
    hce::awaitable::await::policy p = 
        (hce::awaitable::await::policy)
        (state_ & hce::awaitable::interface::await_policy_mask_);
    HCE_TRACE_METHOD_BODY("await_policy",p);
    return p; 
}

hce::awaitable::resume::policy hce::awaitable::interface::resume_policy() const { 
    hce::awaitable::resume::policy p = 
        (hce::awaitable::resume::policy)
        (state_ & hce::awaitable::interface::resume_policy_mask_);
    HCE_TRACE_METHOD_BODY("resume_policy",p);
    return p; 
}

/**
 @brief unblock and resume a suspended operation 

 This should be called by a different coroutine or thread.

 Calling this method will allow the unblock the thread or suspended 
 coroutine (`co_await` will return to its caller). 

 @param m arbitary memory passed to on_resume()
 */
void hce::awaitable::interface::resume(void* m){
    HCE_LOW_METHOD_ENTER("resume");

    auto rp = resume_policy();

    // acquire the lock
    if(rp == hce::awaitable::resume::policy::lock){ lock_(); }

    // call the custom resumption code
    this->on_resume(m); 

    if(has_pointer_()) [[likely]] {
        if(is_coroutine_()) [[likely]] { 
            // unblock the suspended coroutine and push the handle to its 
            // destination. Make sure that handle is unset before passing 
            // to destination. There are certain cases where the the 
            // coroutine can be rescheduled where this can cause an error 
            // otherwise in "no-lock" scenarios.
            HCE_TRACE_METHOD_BODY("resume","to_destination");
            auto& data = get_data_();
            auto h = data.handle;
            has_pointer_(false);

            if(rp != hce::awaitable::resume::policy::no_lock) { 
                unlock_(); 
            }

            this->to_destination(h);
        } else [[unlikely]] {
            // unblock the suspended thread 
            HCE_TRACE_METHOD_BODY("resume","unblock");
            has_pointer_(false);

            if(rp == hce::awaitable::resume::policy::no_lock) { 
                get_data_().this_thread->unblock(); 
            } else { 
                get_data_().this_thread->unblock(*this); 
            }
        }
    } else [[likely]] {
        HCE_TRACE_METHOD_BODY("resume","not blocked");
        // this was called before blocking occurred
        if(rp != hce::awaitable::resume::policy::no_lock) { unlock_(); }
    }
}

bool hce::awaitable::interface::awaited_() const {
    return state_ & hce::awaitable::interface::awaited_mask_;
}

void hce::awaitable::interface::awaited_(bool b) {
    state_ = b 
        ? state_ | hce::awaitable::interface::awaited_mask_ 
        : state_ & ~hce::awaitable::interface::awaited_mask_;
}

bool hce::awaitable::interface::has_pointer_() const {
    return state_ & has_pointer_mask_;
}

void hce::awaitable::interface::has_pointer_(bool b) {
    state_ = b 
        ? state_ | hce::awaitable::interface::has_pointer_mask_ 
        : state_ & ~hce::awaitable::interface::has_pointer_mask_;
}

bool hce::awaitable::interface::is_coroutine_() const {
    return state_ & hce::awaitable::interface::is_coroutine_mask_;
}

void hce::awaitable::interface::is_coroutine_(bool b) {
    state_ = b 
        ? state_ | hce::awaitable::interface::is_coroutine_mask_ 
        : state_ & ~hce::awaitable::interface::is_coroutine_mask_;
}

bool hce::awaitable::interface::locked_() const {
    return state_ & hce::awaitable::interface::locked_mask_;
}

void hce::awaitable::interface::locked_(bool b) {
    state_ = b 
        ? state_ | hce::awaitable::interface::locked_mask_
        : state_ & ~hce::awaitable::interface::locked_mask_;
}

hce::awaitable::interface::data& hce::awaitable::interface::get_data_() {
    return *((hce::awaitable::interface::data*)&(data_));
}

void hce::awaitable::interface::lock_() { 
    HCE_TRACE_METHOD_ENTER("lock");
    // wrap actual lock/unlock calls with state management
    this->lock(); 
    locked_(true);
}

void hce::awaitable::interface::unlock_() { 
    HCE_TRACE_METHOD_ENTER("unlock");
    locked_(false);
    this->unlock(); 
}
