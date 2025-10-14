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

hce::awaitable::interface::interface(uint8_t policy_bits) {
    constexpr uint8_t sanitize_policy_bits = hce::awaitable::masks::await_policy |
                                             hce::awaitable::masks::resumed_policy |
                                             hce::awaitable::masks::notify_policy;

    // ensure no unexpected bits are set
    policy_bits = policy_bits & sanitize_policy_bits;

    /* 
     Initialize the state bits.

     Policy values are also their masks. Bitwise OR-ing a policy flips the 
     appropriate bit to "on" or "off".
     
     Locked state is assumed from the await::policy, which is `0` in the defer 
     case, and non-zero (at the appropriate bit) in the adopt case.

     All other bits start at 0.
     */
    state_ = 
        policy_bits
        // set the locked state based on the await policy
        | ((policy_bits & hce::awaitable::masks::await_policy) 
           ? hce::awaitable::masks::locked 
           : 0x0);

    HCE_LOW_CONSTRUCTOR();
}

hce::awaitable::interface::~interface() { 
    HCE_TRACE_DESTRUCTOR();

    if(state_ & hce::awaitable::masks::suspended) [[unlikely]] {
        if(state_ & hce::awaitable::masks::is_coroutine) {
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

    // no need to call data_ destructor, it holds nothing, or a pointer, or 
    // pointer-like memory and is guaranteed POD.
}

bool hce::awaitable::interface::await_ready() {
    HCE_LOW_METHOD_ENTER("await_ready");

    // acquire the lock if implementation was not constructed with ownership
    if(await_policy() == hce::awaitable::await::policy::defer_lock) { 
        lock(); 
    }

    // set awaited flag
    set_awaited_();

    // check ready state by calling implementation
    if(this->on_ready()) [[unlikely]] {
        HCE_TRACE_METHOD_BODY("await_ready","ready immediately");
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
    set_suspended_();

    // If this handle is valid, we are in a coroutine. Optimize if() check for 
    // coroutine suspends over system threads.
    if(h) [[likely]] {
        HCE_TRACE_METHOD_BODY("await_suspend",h);

        set_is_coroutine_();

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

        unset_is_coroutine_();

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
    // This method is expected to operate without competition for the lock, 
    // because either await_ready() has returned true or notify() has unblocked 
    // the operation.
    HCE_TRACE_METHOD_ENTER("resumed");

    // acquire only the relevant bits we need to check against
    constexpr uint8_t relevant_bits_mask = 
        hce::awaitable::masks::locked | hce::awaitable::masks::resumed_policy;

    // unlocked state is represented by 0 value in bit 0. This is the default 
    // 0 bit value for masks other than hce::awaitable::masks::locked so we 
    // don't need to 0x0 & other_mask when accounting for the unlocked state.
    constexpr uint8_t unlocked_to_unlocked = hce::awaitable::resumed::policy::unlocked;
    constexpr uint8_t unlocked_to_locked = hce::awaitable::resumed::policy::locked;
    constexpr uint8_t locked_to_unlocked = hce::awaitable::masks::locked | 
                                           hce::awaitable::resumed::policy::unlocked;
    constexpr uint8_t locked_to_locked = hce::awaitable::masks::locked | 
                                         hce::awaitable::resumed::policy::locked;

    // Setup a jump table to handle if we need to do nothing, lock or unlock.
    //
    // The purpose of using jump tables in this code is to not only elide some 
    // potential processing, but to flatten the cost of using awaitables, 
    // favoring no particular usecase in terms of throughput efficiency, based 
    // on how complicated if/else trees in these functions are structured.
    //
    // Normally this consideration would be overoptimization. However, this code
    // is:
    // A) guaranteed to be a potential bottleneck for all hce communication 
    // B) written for a framework intended to scale
    //
    // Therefore various protective design choices have been implemented.
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

void hce::awaitable::interface::notify(void* m) {
    HCE_LOW_METHOD_ENTER("resume");

    // the bits we need to extract to jump
    constexpr uint8_t relevant_bits_mask = 
        // if exempt, no lock or unlock. If responsible Only lock if unlocked, 
        // and only unlock if locked before end of the case
        hce::awaitable::masks::suspended |
        hce::awaitable::masks::is_coroutine;

    constexpr uint8_t not_suspended_thread = 0x0;
    constexpr uint8_t not_suspended_coroutine = hce::awaitable::masks::is_coroutine;
    constexpr uint8_t suspended_thread = hce::awaitable::masks::suspended;
    constexpr uint8_t suspended_coroutine = hce::awaitable::masks::suspended |
                                            hce::awaitable::masks::is_coroutine;

    // cannot incorporate this into switch case, because the non-atomic nature 
    // of state_ requires checking it only after we are sure we have the lock.
    if(notify_policy() == hce::awaitable::notify::policy::lock_responsible) {
        // acquire the lock
        lock();

        switch(state_ & relevant_bits_mask) {
            case not_suspended_thread:
            case not_suspended_coroutine:
            {
                HCE_TRACE_METHOD_BODY("resume","not blocked");
                // Call the custom resumption code. The suspended operation will NOT be 
                // resumed yet.
                this->on_notify(m);
                unlock();
                break;
            }
            case suspended_thread:
            {
                HCE_TRACE_METHOD_BODY("resume","unblock thread");
                this->on_notify(m);
                /*
                 Make sure that suspended pointer bit is unset before resuming. 
                 There are certain cases where failing to do this can cause a race 
                 condition error in "no-lock" scenarios when destructing the 
                 interface.
                 */
                unset_suspended_();
                // Resume the suspended thread. At this point it is UNSAFE to 
                // mutate the state of this object further, as its lifetime is 
                // now volatile.
                get_data_().this_thread->unblock(*this);
                break;
            }
            case suspended_coroutine:
            {
                HCE_TRACE_METHOD_BODY("resume","to_destination coroutine");
                // retrieve the stored handle and stash it on the stack while 
                // we hold the lock (get_data_() is volatile outside the lock)
                auto h = get_data_().handle; 
                unset_suspended_();
                this->on_notify(m);
                unlock();
                // Resume the suspended coroutine. At this point it is UNSAFE to 
                // mutate the state of this object further, as its lifetime is 
                // now volatile.
                this->to_destination(h);
                break;
            }
            default:
            {
                std::stringstream ss;
                ss << " cannot process " << *this << " with unknown state(): " << state_;
                HCE_FATAL_METHOD_BODY("resume",ss.str());
                unlock();
                std::terminate();
                break;
            }
        }
    } else {
        // assume we have the lock and are not responsible for it

        switch(state_ & relevant_bits_mask) {
            case not_suspended_thread:
            case not_suspended_coroutine:
            {
                HCE_TRACE_METHOD_BODY("resume","not blocked");
                // Call the custom resumption code. The suspended operation will NOT be 
                // resumed yet.
                this->on_notify(m);
                break;
            }
            case suspended_thread:
            {
                HCE_TRACE_METHOD_BODY("resume","unblock thread");
                unset_suspended_();
                this->on_notify(m);
                get_data_().this_thread->unblock();
                break;
            }
            case suspended_coroutine:
            {
                HCE_TRACE_METHOD_BODY("resume","to_destination coroutine");
                auto h = get_data_().handle;
                unset_suspended_();
                this->on_notify(m);
                this->to_destination(h);
                break;
            }
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
    set_locked_();
}

void hce::awaitable::interface::unlock() { 
    HCE_TRACE_METHOD_ENTER("unlock");
    unset_locked_();
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

void hce::awaitable::interface::default_deleter_(interface* i){
    delete i;
}

uint8_t hce::awaitable::interface::state() const { 
    return state_;
}

hce::awaitable::await::policy hce::awaitable::interface::await_policy() const { 
    hce::awaitable::await::policy p = 
        (hce::awaitable::await::policy)
        (state_ & hce::awaitable::masks::await_policy);
    HCE_TRACE_METHOD_BODY("await_policy",p);
    return p; 
}

hce::awaitable::notify::policy hce::awaitable::interface::notify_policy() const { 
    hce::awaitable::notify::policy p = 
        (hce::awaitable::notify::policy)
        (state_ & hce::awaitable::masks::notify_policy);
    HCE_TRACE_METHOD_BODY("notify_policy",p);
    return p; 
}

hce::awaitable::resumed::policy hce::awaitable::interface::resumed_policy() const { 
    hce::awaitable::resumed::policy p = 
        (hce::awaitable::resumed::policy)
        (state_ & hce::awaitable::masks::resumed_policy);
    HCE_TRACE_METHOD_BODY("resumed_policy",p);
    return p; 
}

void hce::awaitable::interface::set_awaited_() {
    state_ |= hce::awaitable::masks::awaited;
}

void hce::awaitable::interface::set_suspended_() {
    state_ |= hce::awaitable::masks::suspended;
}

void hce::awaitable::interface::set_is_coroutine_() {
    state_ |= hce::awaitable::masks::is_coroutine;
}

void hce::awaitable::interface::set_locked_() {
    state_ |= hce::awaitable::masks::locked;
}

void hce::awaitable::interface::unset_suspended_() {
    state_ &= ~hce::awaitable::masks::suspended;
}

void hce::awaitable::interface::unset_is_coroutine_() {
    state_ &= ~hce::awaitable::masks::is_coroutine;
}

void hce::awaitable::interface::unset_locked_() {
    state_ &= ~hce::awaitable::masks::locked;
}

hce::awaitable::interface::data& hce::awaitable::interface::get_data_() {
    return *((hce::awaitable::interface::data*)data_);
}

void hce::awaitable::wait() {
    HCE_MIN_METHOD_ENTER("wait");
    if(impl_ && !(impl_->state() & hce::awaitable::masks::awaited)) [[unlikely]] {
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
            impl_->await_resume(); // manually await resume
        } else {
            HCE_TRACE_METHOD_BODY("wait","thread done");
            impl_->await_resume(); 
        }
    } else {
        HCE_TRACE_METHOD_BODY("wait","nothing to do");
    }
}
