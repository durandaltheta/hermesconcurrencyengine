//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#include <memory>
#include <unordered_set>

#include "utility.hpp"
#include "thread.hpp"
#include "scheduler.hpp"
#include "thread_key_map.hpp"

hce::scheduler*& hce::detail::scheduler::tl_this_scheduler() {
    thread_local hce::thread::local::ptr<hce::thread::key::scheduler> p;
    return p.ref();
}

std::unique_ptr<hce::list<std::coroutine_handle<>>>*& 
hce::detail::scheduler::tl_this_scheduler_local_queue() {
    thread_local hce::thread::local::ptr<hce::thread::key::scheduler_local_queue> p;
    return p.ref();
}

hce::scheduler_halted_exception::scheduler_halted_exception(scheduler* sch) : 
    estr([&]() -> std::string {
        std::stringstream ss;
        ss << "schedule() failed because scheduler is halted:" << sch;
        return ss.str();
    }())
{ }

const char* hce::scheduler_halted_exception::what() const noexcept { 
    return estr.c_str(); 
}

hce::awaitable_destroyed_without_joining_result::awaitable_destroyed_without_joining_result(void* c, void* j) :
    estr([&]() -> std::string {
        std::stringstream ss;
        ss << "std::coroutine_handle<>@" 
           << (void*)c
           << " was destroyed before it was completed, so " 
           << (hce::printable*)j
           << " cannot join with it";
        return ss.str();
    }()) 
{ }

const char* hce::awaitable_destroyed_without_joining_result::what() const noexcept { 
    return estr.c_str(); 
}


hce::scheduler::migrater::migrater(hce::scheduler& destination) : 
    destination_(&destination) 
{
    HCE_LOW_CONSTRUCTOR();
}

hce::scheduler::migrater::~migrater() { HCE_LOW_DESTRUCTOR(); }

std::string hce::scheduler::migrater::info_name() { 
    return "hce::scheduler::migrater"; 
}

std::string hce::scheduler::migrater::name() const { 
    return migrater::info_name(); 
}

bool hce::scheduler::migrater::await_ready() { 
    HCE_MIN_METHOD_ENTER("await_ready");

    if(hce::scheduler::in()) [[likely]] {
        return false;
    } else [[unlikely]] {
        return true;
    }
}

void hce::scheduler::migrater::await_suspend(std::coroutine_handle<> h) {
    HCE_MIN_METHOD_ENTER("await_suspend");
    coroutine::local().release(); 
    destination_->schedule_(std::move(h));
}

void hce::scheduler::migrater::await_resume() { 
    HCE_MIN_METHOD_ENTER("await_resume");
}


hce::scheduler::lifecycle::manager::manager() : state_(executing) { 
    HCE_HIGH_CONSTRUCTOR(); 
}

hce::scheduler::lifecycle::manager::~manager() { 
    HCE_HIGH_DESTRUCTOR(); 
}

std::string hce::scheduler::lifecycle::manager::info_name() { 
    return "hce::scheduler::lifecycle::manager"; 
}

std::string hce::scheduler::lifecycle::manager::name() const { 
    return manager::info_name(); 
}

std::string hce::scheduler::lifecycle::manager::content() const { 
    std::stringstream ss;
    auto it = lifecycle_pointers_.begin();
    auto end = lifecycle_pointers_.end();

    if(it != end) {
        ss << **it;
        ++it;

        for(; it!=end; ++it) {
            ss << ", " << **it;
        }
    }

    return ss.str();
}

void hce::scheduler::lifecycle::manager::registration(
        std::unique_ptr<scheduler::lifecycle> lptr) 
{
    if(lptr) {
        HCE_HIGH_METHOD_ENTER("registration", *lptr);

        std::lock_guard<hce::spinlock> lk(lk_);

        // Synchronize the new lifecycle with the global state
        if(state_ == executing) { lptr->resume(); }
        else if(state_ == suspended) { lptr->suspend(); }

        lifecycle_pointers_.push_back(std::move(lptr)); 
    }
}

void hce::scheduler::lifecycle::manager::suspend() {
    HCE_HIGH_METHOD_ENTER("suspend");
    std::lock_guard<hce::spinlock> lk(lk_);

    state_ = suspended;
    for(auto& lp : lifecycle_pointers_) { lp->suspend(); }
}

void hce::scheduler::lifecycle::manager::resume() {
    HCE_HIGH_METHOD_ENTER("resume");
    std::lock_guard<hce::spinlock> lk(lk_);

    if(state_ == suspended) {
        state_ = executing;
        for(auto& lp : lifecycle_pointers_) { lp->resume(); }
    }
}

hce::scheduler::lifecycle::~lifecycle(){ 
    HCE_HIGH_DESTRUCTOR(); 
    sch_->halt_(); // halt the scheduler
    thd_.join(); // join the scheduler's thread
}

std::string hce::scheduler::lifecycle::info_name() { 
    return "hce::scheduler::lifecycle"; 
}

std::string hce::scheduler::lifecycle::name() const { return lifecycle::info_name(); }

std::string hce::scheduler::lifecycle::content() const {
    std::stringstream ss;
    ss << sch_.get() << ", std::thread::id@" << thd_.get_id();
    return ss.str();
}

hce::scheduler& hce::scheduler::lifecycle::get_scheduler() { 
    HCE_HIGH_METHOD_ENTER("get_scheduler");
    return *sch_; 
}

void hce::scheduler::lifecycle::suspend() { 
    HCE_HIGH_METHOD_ENTER("suspend");
    return sch_->suspend_(); 
}

void hce::scheduler::lifecycle::resume() { 
    HCE_HIGH_METHOD_ENTER("resume");
    return sch_->resume_(); 
}

hce::scheduler::lifecycle::lifecycle(std::shared_ptr<hce::scheduler> sch) :
    sch_(std::move(sch)),
    thd_([](hce::scheduler* sch) { sch->run(); }, sch_.get())
{
    HCE_HIGH_CONSTRUCTOR();
}

hce::scheduler::global::~global() { HCE_HIGH_DESTRUCTOR(); }

std::string hce::scheduler::global::info_name() { 
    return "hce::scheduler::global"; 
}

std::string hce::scheduler::global::name() const { return global::info_name(); }

/// return the process-wide scheduler instance
hce::scheduler& hce::scheduler::global::get_scheduler() { return sch_; }

hce::scheduler::global::global() :
    sch_([]() -> hce::scheduler& {
        auto lf = hce::scheduler::make(hce::config::scheduler::global::config());
        hce::scheduler& sch = lf->get_scheduler();
        hce::service<lifecycle::manager>::get().registration(std::move(lf));
        return sch;
    }())
{ 
    HCE_HIGH_CONSTRUCTOR();
}

hce::scheduler::~scheduler() { HCE_HIGH_DESTRUCTOR(); }

std::string hce::scheduler::info_name() { return "hce::scheduler"; }
std::string hce::scheduler::name() const { return scheduler::info_name(); }

std::unique_ptr<hce::scheduler::lifecycle> hce::scheduler::make(
        hce::config::scheduler::config c)  
{
    HCE_HIGH_FUNCTION_ENTER("hce::scheduler::make");

    // make the shared pointer
    std::shared_ptr<scheduler> s(new scheduler(c));

    // finish initialization and configure the scheduler's runtime behavior
    s->finalize_(s);

    // allocate and return the lifecycle pointer 
    lifecycle* lp = new lifecycle(std::move(s));
    return std::unique_ptr<lifecycle>(lp);
}

bool hce::scheduler::in() {
    bool b = detail::scheduler::tl_this_scheduler();
    HCE_TRACE_FUNCTION_ENTER("hce::scheduler::in",b);
    return b; 
}

hce::scheduler& hce::scheduler::local() {
    HCE_TRACE_FUNCTION_ENTER("hce::scheduler::local");
    return *(detail::scheduler::tl_this_scheduler());
}

hce::scheduler& hce::scheduler::get() {
    HCE_TRACE_FUNCTION_ENTER("hce::scheduler::get");
    return scheduler::in() 
        ? scheduler::local()
        : hce::service<global>::get().get_scheduler();
}

bool hce::scheduler::operator==(const scheduler& rhs) const {
    HCE_TRACE_METHOD_ENTER("operator ==(const scheduler&)");
    return this == &rhs;
}

bool hce::scheduler::operator!=(const scheduler& rhs) const {
    HCE_TRACE_METHOD_ENTER("operator !=(const scheduler&)");
    return this != &rhs;
}

hce::scheduler::operator std::shared_ptr<scheduler>() {
    HCE_TRACE_METHOD_ENTER("operator std::shared_ptr<scheduler>()");
    return self_wptr_.lock(); 
}

hce::scheduler::operator std::weak_ptr<scheduler>() {
    HCE_TRACE_METHOD_ENTER("operator std::weak_ptr<scheduler>()");
    return self_wptr_; 
}

int hce::scheduler::loglevel() const {
    auto l = config_.loglevel;
    HCE_TRACE_METHOD_BODY("loglevel",l);
    return l;
}

hce::scheduler::state hce::scheduler::status() const {
    state s;

    {
        std::lock_guard<spinlock> lk(lk_);
        s = state_;
    }

    HCE_MIN_METHOD_BODY("status",s);
    return s;
}

size_t hce::scheduler::scheduled_count() const {
    size_t c;

    {
        std::lock_guard<spinlock> lk(lk_);
        c = batch_size_ + coroutine_queue_->size();
    }
    
    HCE_TRACE_METHOD_BODY("workload",c);
    return c;
}

size_t hce::scheduler::reusable_coroutine_handle_limit() const {
    HCE_MIN_METHOD_BODY("reusable_coroutine_handle_limit",config_.reusable_coroutine_handle_limit);
    return config_.reusable_coroutine_handle_limit;
}

hce::scheduler::migrater hce::scheduler::migrate() {
    return hce::scheduler::migrater(*this);
}

hce::scheduler::scheduler(const hce::config::scheduler::config& cfg) : 
    config_(cfg),
    state_(executing), 
    coroutine_queue_(
        new hce::list<std::coroutine_handle<>>(
            hce::pool_allocator<std::coroutine_handle<>>(
                config_.reusable_coroutine_handle_limit)))
{ 
    HCE_HIGH_CONSTRUCTOR();
    reset_flags_(); // initialize flags
}

void hce::scheduler::finalize_(std::shared_ptr<scheduler>& self) {
    // set the weak_ptr
    self_wptr_ = self;
}

void hce::scheduler::schedule_(std::coroutine_handle<> h) {
    if(this == detail::scheduler::tl_this_scheduler()) [[likely]] {
        HCE_TRACE_METHOD_BODY("schedule_","pushing ",h," onto local queue");
        // scheduling inside call to executing scheduler::run(), can do a 
        // lockfree push to local queue 
        (*detail::scheduler::tl_this_scheduler_local_queue())->push_back(h);
    } else [[unlikely]] {
        HCE_TRACE_METHOD_BODY("schedule_","pushing ",h," onto remote queue");

        std::lock_guard<spinlock> lk(lk_);

        if(state_ == halted) [[unlikely]] {
            throw scheduler_halted_exception(this);
        }

        coroutine_queue_->push_back(h);
        coroutines_notify_();
    }
}

void hce::scheduler::suspend_() {
    std::lock_guard<hce::spinlock> lk(lk_);

    if(state_ != halted) { 
        state_ = suspended;

        // wakeup scheduler if necessary from waiting for tasks to force 
        // run() to exit
        coroutines_notify_();
    }
}

void hce::scheduler::resume_() {
    std::lock_guard<spinlock> lk(lk_);
   
    if(state_ == suspended) { 
        state_ = executing; 
        resume_notify_();
    }
}

void hce::scheduler::halt_() {
    std::lock_guard<hce::spinlock> lk(lk_);

    if(state_ != halted) {
        // set the scheduler to the  state
        state_ = halted;

        // resume scheduler if necessary
        resume_notify_();

        // wakeup scheduler if necessary
        coroutines_notify_();
    }
}

void hce::scheduler::reset_flags_() {
    batch_size_ = 0;
    waiting_for_resume_ = false;
    waiting_for_coroutines_ = false;
}

void hce::scheduler::resume_notify_() {
    // only do notify if necessary
    if(waiting_for_resume_) {
        HCE_TRACE_METHOD_BODY("resume_notify_");
        waiting_for_resume_ = false;
        resume_cv_.notify_one();
    }
}

void hce::scheduler::coroutines_notify_() {
    // only do notify if necessary
    if(waiting_for_coroutines_) {
        HCE_TRACE_METHOD_BODY("coroutines_notify_");
        waiting_for_coroutines_ = false;
        coroutines_cv_.notify_one();
    }
}

void hce::scheduler::run() {
    // the local queue of coroutines to evaluate, won't do any logging 
    // because it is an unallocated std:: object
    std::unique_ptr<hce::list<std::coroutine_handle<>>> local_queue(
        new hce::list<std::coroutine_handle<>>(
            hce::pool_allocator<std::coroutine_handle<>>(
                config_.reusable_coroutine_handle_limit)));

    // manage the thread_local pointers for this scheduler with RAII
    struct scoped_locals {
        scoped_locals(
                size_t loglevel,
                scheduler* s, 
                std::unique_ptr<hce::list<std::coroutine_handle<>>>* q) :
            prev_loglevel_(hce::logger::thread_log_level())
        { 
            hce::logger::thread_log_level(loglevel);
            detail::scheduler::tl_this_scheduler() = s;
            detail::scheduler::tl_this_scheduler_local_queue() = q;
        }

        ~scoped_locals() {
            detail::scheduler::tl_this_scheduler_local_queue() = nullptr;
            detail::scheduler::tl_this_scheduler() = nullptr;
            hce::logger::thread_log_level(prev_loglevel_);
        }

    private:
        size_t prev_loglevel_;
    };

    scoped_locals stl(config_.loglevel, this, &local_queue);

    HCE_HIGH_METHOD_ENTER("run");

    // push_back any remaining coroutines back into the main queue. Lock must be
    // held before this is called.
    auto cleanup_batch = [&] {
        // reset scheduler batch evaluating count 
        batch_size_ = 0; 

        // Concatenate every uncompleted coroutine to the back of the 
        // scheduler's main coroutine queue. Concatenation is constant time 
        // with hce::list<T>.
        coroutine_queue_->concatenate(*local_queue);
    };

    // acquire the lock
    std::unique_lock<spinlock> lk(lk_);

    try {
        // if halted return immediately
        while(state_ != halted) [[likely]] {

            // block until no longer suspended
            while(state_ == suspended) { 
                HCE_HIGH_METHOD_BODY("run","suspended");
                // wait for resumption
                waiting_for_resume_ = true;
                resume_cv_.wait(lk);
            }
        
            HCE_HIGH_METHOD_BODY("run","executing");

            /*
             Evaluation loop runs fairly continuously. 99.9% of the time 
             it is expected a scheduler is executing code within this loop
             */
            while(state_ == executing) [[likely]] {

                // check for waiting coroutines
                if(coroutine_queue_->size()) [[likely]] {
                    /*
                     Acquire the current batch of coroutines by trading the 
                     empty local queue with the scheduler's main queue, 
                     reducing lock contention by collecting the entire batch 
                     via a pointer swap.
                     */
                    std::swap(local_queue, coroutine_queue_);

                    // update API accessible batch count
                    batch_size_ = local_queue->size();

                    /* 
                     Unlock scheduler when running executing coroutines to 
                     allow public API to acquire the lock.
                     */
                    lk.unlock();

                    // scope any local variables
                    {
                        size_t count = local_queue->size();

                        // this object is scoped to enable RAII of handles
                        coroutine co;

                        /*
                         Evaluate the batch of coroutines once through,
                         deterministically exiting this loop so the 
                         scheduler can re-evaluate other state.
                         */
                        while(count) [[likely]] { 
                            // decrement from our initial batch count
                            --count;

                            // Get a new task from the front of the task 
                            // queue, cleaning up the old coroutine handle.
                            co.reset(local_queue->front());
                            local_queue->pop();

                            // execute the coroutine
                            co.resume();

                            // check if the coroutine still has a handle
                            if(co) [[unlikely]] {
                                if(!co.done()) [[likely]] {
                                    // locally re-enqueue coroutine 
                                    local_queue->push_back(co.release()); 
                                }
                            } // else coroutine was suspended during await
                        }
                    } // make sure last coroutine is cleaned up before lock

                    // reacquire lock
                    lk.lock(); 

                    // cleanup batch results, requeueing local coroutines
                    cleanup_batch();
                } else [[unlikely]] {
                    // wait for more tasks
                    waiting_for_coroutines_ = true;
                    coroutines_cv_.wait(lk);
                }
            }

            // reset member state flags
            reset_flags_();
        }
    } catch(...) { // catch all other exceptions 
        // it is an error in this framework if an exception occurs when 
        // the lock is held, it should only be when executing user 
        // coroutines that this can even occur
        lk.lock();

        cleanup_batch();

        lk.unlock();

        std::rethrow_exception(std::current_exception());
    }

    HCE_HIGH_METHOD_BODY("run","halted");
}
