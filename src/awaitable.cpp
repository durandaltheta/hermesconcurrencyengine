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
     
     Locked state is assumed from the await::policy, which is `0` in the defer 
     case, and non-zero in the adopt case.

     All other bits start at 0.
     */
    state_((ap ? hce::awaitable::locked_mask : 0x0) 
           | ap 
           | rdp
           | rp)
{ 
    HCE_LOW_CONSTRUCTOR();
}

hce::awaitable::interface::~interface() { 
    HCE_TRACE_DESTRUCTOR();

    if(is_suspended_()) [[unlikely]] {
        if(is_coroutine_()) {
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
        } else {
            std::stringstream ss;
            ss << *this << " was not resumed before being destroyed; it held this_thread: " << get_data_().this_thread;
            HCE_FATAL_METHOD_BODY("~interface", ss.str().c_str());
            std::terminate();  // Or unblock if recoverable, but terminate matches coroutine behavior
        }
    }

    // no need to call data_ destructor, it holds nothing, or pointer or 
    // pointer-like memory and is guaranteed POD.
}

bool hce::awaitable::interface::await_ready() {
    HCE_LOW_METHOD_ENTER("await_ready");

    // acquire the lock if implementation was not constructed with ownership
    if(await_policy() == hce::awaitable::await::policy::defer_lock) { 
        lock(); 
    }

    // set awaited flag
    set_awaited_(true);

    // check ready state by calling implementation
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
void hce::awaitable::interface::await_suspend(std::coroutine_handle<> h) {
    HCE_LOW_METHOD_ENTER("await_suspend");

    // still locked from await_ready()
    this->on_suspend();
    set_suspended_(true);

    // If this handle is valid, we are in a coroutine. Optimize if() check for 
    // coroutine suspends over system threads
    if(h) [[likely]] {
        HCE_TRACE_METHOD_BODY("await_suspend",h);

        set_is_coroutine_(true);

        // Assign the handle to our member. Need to placement new initialize 
        // data to a coroutine handle
        new ((hce::awaitable::interface::data*)data_)
        hce::awaitable::interface::data{h};
        unlock();  // release lock before suspending coroutine

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

        set_is_coroutine_(false);

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
    }
}

void hce::awaitable::interface::await_resume() {
    HCE_TRACE_METHOD_ENTER("resumed");

    constexpr uint8_t relevant_bits_mask = 
        hce::awaitable::locked_mask | hce::awaitable::resumed_policy_mask;

    constexpr uint8_t unlocked_to_unlocked = hce::awaitable::resumed::policy::unlocked;
    constexpr uint8_t unlocked_to_locked = hce::awaitable::resumed::policy::locked;
    constexpr uint8_t locked_to_unlocked = 
        hce::awaitable::locked_mask | hce::awaitable::resumed::policy::unlocked;
    constexpr uint8_t locked_to_locked = 
        hce::awaitable::locked_mask | hce::awaitable::resumed::policy::locked;

    // setup a jump table to handle if we need to do nothing, lock or unlock
    switch(state_ & relevant_bits_mask) {
        case unlocked_to_unlocked:
            // no change
            break;
        case unlocked_to_locked:
            lock();
            break;
        case locked_to_unlocked:
            unlock();
            break;
        case locked_to_locked:
            // no change
            break;
        default:
        {
            std::stringstream ss;
            ss << " cannot process " << *this << " with unknown state(): " << state_;
            HCE_FATAL_METHOD_BODY("await_resume",ss.str());
            std::terminate();
            break;
        }
    }
}

void hce::awaitable::interface::resume(void* m) {
    HCE_LOW_METHOD_ENTER("resume");

    // the bits we need to extract to jump
    constexpr uint8_t relevant_bits_mask = 
        // if exempt, no lock or unlock. If responsible Only lock if unlocked, 
        // and only unlock if locked before end of the case
        hce::awaitable::suspended_mask |
        hce::awaitable::is_coroutine_mask;

    constexpr uint8_t not_suspended_thread = 0x0;
    constexpr uint8_t not_suspended_coroutine = hce::awaitable::is_coroutine_mask;
    constexpr uint8_t suspended_thread = hce::awaitable::suspended_mask;

    constexpr uint8_t suspended_coroutine = 
        hce::awaitable::suspended_mask |
        hce::awaitable::is_coroutine_mask;

    // trivially inlined lambdas
    auto not_suspended = [&]{
        HCE_TRACE_METHOD_BODY("resume","not blocked");
        // Call the custom resumption code. The suspended operation will NOT be 
        // resumed yet.
        state_ = state_ | hce::awaitable::ready_mask;
        this->on_resume(m);
    };

    auto thd_suspended = [&]{
        HCE_TRACE_METHOD_BODY("resume","unblock thread");
        /*
         Make sure that suspended pointer bit is unset before resuming. 
         There are certain cases where failing to do this can cause a race 
         condition error in "no-lock" scenarios when destructing the 
         interface.
         */
        set_suspended_(false);
        this->on_resume(m);
    };

    auto coro_suspended = [&]{
        HCE_TRACE_METHOD_BODY("resume","to_destination coroutine");
        set_suspended_(false);
        this->on_resume(m);
    };

    // cannot incorporate this into switch case, because the non-atomic nature 
    // of state_ requires checking it only after we are sure we have the lock.
    if(resume_policy() == hce::resume::policy::lock_responsible) {
        lock();

        switch(state_ & relevant_bits_mask) {
            case not_suspended_thread:
                not_suspended();
                unlock();
                break;
            case not_suspended_coroutine:
                not_suspended();
                unlock();
                break;
            case suspended_thread:
                thd_suspended();
                // Resume the suspended thread. At this point it is UNSAFE to 
                // mutate the state of this object further, as its lifetime is 
                // now volatile.
                get_data_().this_thread->unblock(*this);
                break;
            case suspended_coroutine:
                auto h = get_data_().handle; // retrieve the stored handle
                // Resume the suspended coroutine. At this point it is UNSAFE to 
                // mutate the state of this object further, as its lifetime is 
                // now volatile.
                coro_suspended();
                unlock();
                this->to_destination(h);
                break;
            default:
            {
                std::stringstream ss;
                ss << " cannot process " << *this << " with unknown state(): " << state_;
                HCE_FATAL_METHOD_BODY("resume",ss.str());
                std::terminate();
                break;
            }
        }
    } else {
        // assume we have the lock and are not responsible for it
        switch(state_ & relevant_bits_mask) {
            case not_suspended_thread:
                not_suspended();
                break;
            case not_suspended_coroutine:
                not_suspended();
                break;
            case suspended_thread:
                thd_suspended();
                get_data_().this_thread->unblock();
                break;
            case suspended_coroutine:
                auto h = get_data_().handle;
                coro_suspended();
                this->to_destination(h);
                break;
            default:
            {
                std::stringstream ss;
                ss << " cannot process " << *this << " with unknown state(): " << state_;
                HCE_FATAL_METHOD_BODY("resume",ss.str());
                std::terminate();
                break;
            }
        }
    }
}

void hce::awaitable::interface::lock() { 
    HCE_TRACE_METHOD_ENTER("lock");
    this->lock_impl(); 
    set_locked_(true);
}

void hce::awaitable::interface::unlock() { 
    HCE_TRACE_METHOD_ENTER("unlock");
    set_locked_(false);
    this->unlock_impl(); 
}

std::string hce::awaitable::interface::info_name() { 
    return "hce::awaitable::interface"; 
}

std::string hce::awaitable::interface::name() const { 
    return hce::awaitable::interface::info_name(); 
}
        
hce::awaitable::interface::deleter_t hce::awaitable::interface::deleter() {
    return default_deleter_;
}

bool hce::awaitable::interface::is_locked_() const {
    return state_ & hce::awaitable::locked_mask;
}

void hce::awaitable::interface::default_deleter_(interface* i){
    delete i;
}

uint8_t hce::awaitable::interface::state() const { 
    return state_;
}

bool hce::awaitable::interface::is_awaited_() const {
    return state_ & hce::awaitable::awaited_mask;
}

bool hce::awaitable::interface::is_suspended_() const {
    return state_ & hce::awaitable::suspended_mask;
}

bool hce::awaitable::interface::is_coroutine_() const {
    return state_ & hce::awaitable::is_coroutine_mask;
}

hce::awaitable::await::policy hce::awaitable::interface::await_policy() const { 
    hce::awaitable::await::policy p = 
        (hce::awaitable::await::policy)
        (state_ & hce::awaitable::await_policy_mask);
    HCE_TRACE_METHOD_BODY("await_policy",p);
    return p; 
}

hce::awaitable::resume::policy hce::awaitable::interface::resume_policy() const { 
    hce::awaitable::resume::policy p = 
        (hce::awaitable::resume::policy)
        (state_ & hce::awaitable::resume_policy_mask);
    HCE_TRACE_METHOD_BODY("resume_policy",p);
    return p; 
}

hce::awaitable::resumed::policy hce::awaitable::interface::resumed_policy() const { 
    hce::awaitable::resumed::policy p = 
        (hce::awaitable::resumed::policy)
        (state_ & hce::awaitable::resumed_policy_mask);
    HCE_TRACE_METHOD_BODY("resumed_policy",p);
    return p; 
}

void hce::awaitable::interface::set_awaited_(bool b) {
    state_ = b 
        ? state_ | hce::awaitable::awaited_mask 
        : state_ & ~hce::awaitable::awaited_mask;
}

void hce::awaitable::interface::set_suspended_(bool b) {
    state_ = b 
        ? state_ | hce::awaitable::suspended_mask 
        : state_ & ~hce::awaitable::suspended_mask;
}

void hce::awaitable::interface::set_is_coroutine_(bool b) {
    state_ = b 
        ? state_ | hce::awaitable::is_coroutine_mask 
        : state_ & ~hce::awaitable::is_coroutine_mask;
}

void hce::awaitable::interface::set_locked_(bool b) {
    state_ = b 
        ? state_ | hce::awaitable::locked_mask
        : state_ & ~hce::awaitable::locked_mask;
}

hce::awaitable::interface::data& hce::awaitable::interface::get_data_() {
    return *((hce::awaitable::interface::data*)data_);
}

void hce::awaitable::wait() {
    HCE_MIN_METHOD_ENTER("wait");
    if(impl_ && !(impl_->state() & hce::awaitable::awaited_mask)) [[unlikely]] {
        if(coroutine::in()) [[unlikely]] { 
            // coroutine failed to `co_await` the awaitable
            std::stringstream ss;
            ss << hce::coroutine::local()
               << " did not call co_await on "
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
