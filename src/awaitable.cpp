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
                                     hce::awaitable::resumed::policy rdp,
                                     hce::awaitable::resume::policy rp) : 
    /* 
     Initialize the state bits.

     Policy values are also their masks. Bitwise OR-ing a policy flips the 
     appropriate bit to "on" or "off".
     
     Locked state is assumed from the await policy.

     All other bits start at 0.
     */
    state_((ap == hce::awaitable::await::policy::adopt_lock 
              ? hce::awaitable::interface::locked_mask_ 
              : 0x0) 
            | ap 
            | rdp 
            | rp)
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
    if(await_policy() == hce::awaitable::await::policy::defer_lock) { 
        lock(); 
    }

    // set awaited flag
    awaited_(true);

    // call the ready code
    if(this->on_ready()) [[unlikely]] {
        HCE_TRACE_METHOD_BODY("await_ready","ready immediately");
        unlock();
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

        is_coroutine_(true);

        // Assign the handle to our member. Need to placement new initialize 
        // data to a coroutine handle
        new ((hce::awaitable::interface::data*)&(data_)) 
        hce::awaitable::interface::data{h};

        // the current coroutine no longer manages the handle
        hce::coroutine::local().release(); 

        /* 
         Compiler now returns to the caller of coroutine::resume() 
         when this function returns. For example, if coroutine was running in 
         an hce::scheduler, control returns to the hce::scheduler coroutine 
         processing loop. The suspended coroutine will be resumed when
         hce::awaitable::resume() is called.
         */
    } else [[unlikely]] {
        /* 
         Block the calling thread using traditional mechanisms.

         Behavior of system thread in this function is VERY different than in 
         coroutines. We block here on a condition variable until 
         hce::awaitable::resume() is called.
         */

        is_coroutine_(false);

        // placement new initialize data to an awaitable::this_thread pointer
        new ((hce::awaitable::interface::data*)&(data_)) 
        hce::awaitable::interface::data{
            hce::detail::awaitable::this_thread::get()
        };

        HCE_TRACE_METHOD_BODY("await_suspend",
                              "this_thread:",
                              (void*)(get_data_().this_thread));

        // allow condition_variable::wait() to suspend and unlock `this` 
        hce::detail::awaitable::this_thread::block(*this);

        // we are now resumed
    }
}
        
hce::awaitable::interface::deleter_t hce::awaitable::interface::deleter() {
    return default_deleter_;
}

void hce::awaitable::interface::clean() {
    // sanity guard to ensure our lock is released when the awaitable instance 
    // is cleaned up
    if(locked()) { 
        unlock();
    }
}

bool hce::awaitable::interface::ready() const {
    return state_ & hce::awaitable::interface::ready_mask_;
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

hce::awaitable::resumed::policy hce::awaitable::interface::resumed_policy() const { 
    hce::awaitable::resumed::policy p = 
        (hce::awaitable::resumed::policy)
        (state_ & hce::awaitable::interface::await_policy_mask_);
    HCE_TRACE_METHOD_BODY("resumed_policy",p);
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
void hce::awaitable::interface::resume(void* m) {
    HCE_LOW_METHOD_ENTER("resume");

    auto rp = resume_policy();

    if(rp == hce::awaitable::resume::policy::lock){ 
        // acquire the lock
        lock(); 
    }

    // Call the custom resumption code. The suspended operation will NOT be 
    // resumed yet.
    this->on_resume(m); 

    if(has_pointer_()) [[likely]] {
        auto rdp = resumed_policy();

        if(is_coroutine_()) [[likely]] { 
            /*
             Unblock the suspended coroutine and push the handle to its 
             destination. 
             */
            HCE_TRACE_METHOD_BODY("resume","to_destination");
            auto h = get_data_().handle; // retrieve the stored handle

            /*
             Make sure that pointer bit is unset before passing 
             to destination. There are certain cases where failing to do this 
             can cause a race condition error in "no-lock" scenarios.
             */
            has_pointer_(false);

            if(rdp == hce::awaitable::resumed::policy::locked) { 
                lock(); 
            } else {
                unlock();
            }

            // resume the suspended coroutine
            this->to_destination(h);
        } else [[unlikely]] {
            // unblock the suspended thread 
            HCE_TRACE_METHOD_BODY("resume","unblock");
            has_pointer_(false);

            if(rp == hce::awaitable::resume::policy::lock &&
               rdp == hce::awaitable::resumed::policy::unlocked) { 
                // resume the waiting thread
                get_data_().this_thread->unblock(*this); 
            } else { 
                // resume the waiting thread without unlocking
                get_data_().this_thread->unblock(); 
            }
        }
    } else [[unlikely]] {
        HCE_TRACE_METHOD_BODY("resume","not blocked");
        // this was called before blocking occurred
        if(rp == hce::awaitable::resume::policy::lock) { unlock(); }
    }
}

bool hce::awaitable::interface::locked() const {
    return state_ & hce::awaitable::interface::locked_mask_;
}

void hce::awaitable::interface::lock() { 
    HCE_TRACE_METHOD_ENTER("lock");

    // only change state when possible
    if(can_lock_()) {
        // wrap actual lock/unlock calls with state management
        this->lock_impl(); 
        locked_(true);
    }
}

void hce::awaitable::interface::unlock() { 
    HCE_TRACE_METHOD_ENTER("unlock");

    // only change state when possible
    if(can_unlock_()) {
        locked_(false);
        this->unlock_impl(); 
    }
}

void hce::awaitable::interface::default_deleter_(interface* i){
    delete i;
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

hce::awaitable::interface::data& hce::awaitable::interface::get_data_() {
    return *((hce::awaitable::interface::data*)&(data_));
}

bool hce::awaitable::interface::can_lock_() {
    if(locked()) {
        return false;
    } else {
        if(!is_coroutine_() &&
           get_data_().this_thread->blocked() &&
           resumed_policy() == hce::awaitable::resumed::policy::unlocked) {
            // don't allow std::condition_variable_any in this_thread to 
            // reacquire the lock when we don't want it to
            return false;
        } else {
            return true;
        }
    }
}

bool hce::awaitable::interface::can_unlock_() {
    return locked();
}

void hce::awaitable::interface::locked_(bool b) {
    state_ = b 
        ? state_ | hce::awaitable::interface::locked_mask_
        : state_ & ~hce::awaitable::interface::locked_mask_;
}
