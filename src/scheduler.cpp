//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#include <thread>
#include <memory>
#include <mutex>
#include <deque>

#include "atomic.hpp"
#include "scheduler.hpp"

hce::scheduler*& hce::detail::scheduler::tl_this_scheduler() {
    thread_local hce::scheduler* tlts = nullptr;
    return tlts;
}

hce::scheduler::lifecycle::manager& hce::scheduler::lifecycle::manager::instance() {  
    static hce::scheduler::lifecycle::manager m;
    return m;
}

extern std::unique_ptr<hce::scheduler::config> hce_scheduler_global_config();

#ifndef HCEGLOBALREUSEBLOCKPROCS
#define HCEGLOBALREUSEBLOCKPROCS 1
#endif 

#ifndef HCECUSTOMGLOBALCONFIG
std::unique_ptr<hce::scheduler::config> hce_scheduler_global_config() {
    auto config = hce::scheduler::config::make();
    config->block_workers_reuse_count = HCEGLOBALREUSEBLOCKPROCS;
    return config;
}
#endif

bool& hce::scheduler::blocking::worker::tl_is_worker() {
    thread_local bool is_wkr = false;
    return is_wkr;
}

hce::scheduler& hce::scheduler::global_() {
    static std::shared_ptr<hce::scheduler> sch(
        []() -> std::shared_ptr<hce::scheduler> {
            auto i = hce::scheduler::make(hce_scheduler_global_config());
            std::shared_ptr<hce::scheduler> sch = i->scheduler();
            HCE_MED_FUNCTION_BODY("hce::scheduler::global_",*sch);
            std::thread([](std::shared_ptr<hce::scheduler::install> i) { }, std::move(i)).detach();
            return sch;
        }());
    return *sch;
}

void hce::scheduler::configure_(
        std::shared_ptr<scheduler>& self, 
        std::unique_ptr<config> cfg) {
    // set the weak_ptr
    self_wptr_ = self;

    // ensure our config is allocated
    if(cfg) { config_ = std::move(cfg); }
    else { config_ = config::make(); }

    // set our block reuse threadpool
    worker_thread_reuse_cnt_ = config_->block_workers_reuse_count;
}

void hce::scheduler::run() {
    // check the thread local scheduler pointer to see if we're already in 
    // a scheduler and error out immediately if called improperly
    if(hce::coroutine::in()) { 
        throw hce::scheduler::cannot_install_in_a_coroutine_exception(); 
    }

    // RAII thread log level management
    struct scoped_log_level {
        scoped_log_level(int requested_log_level) : 
            // stash the current log level and restore when going out of scope
            parent_log_level_(hce::printable::thread_log_level()) 
        { 
            // ensure the proper log level is set at the point 
            hce::printable::thread_log_level(requested_log_level);
        }

        ~scoped_log_level() {
            hce::printable::thread_log_level(parent_log_level_);
        }

    private:
        size_t parent_log_level_;
    };

    // set the necessary log level for the worker thread
    scoped_log_level sll(config_->log_level);
    
    HCE_HIGH_METHOD_ENTER("run");

    // assume scheduler has never been run before, run_ will handle the real 
    // state in a synchronized way
    bool cont = true;

    auto handle_exception = [&]{
        if(config_->on_exception.count()) {
            // Call exception handlers, allow them to rethrow if 
            // necessary. `scheduler::current_exception()` contains the 
            // thrown exception pointer.
            HCE_ERROR_METHOD_BODY("run","hce::scheduler::config::on_exception.call()");
            config_->on_exception.call(*this);
        } else {
            HCE_ERROR_METHOD_BODY("run","hce::scheduler::config::on_exception has no handlers, rethrowing");
            std::rethrow_exception(std::current_exception());
        }
    };
    
    HCE_HIGH_METHOD_BODY("run","hce::scheduler::config::on_init.call()");

    // call initialization handlers
    config_->on_init.call(*this);

    do {
        try {
            // inner run call evaluates coroutines and timers
            cont = run_();
        } catch(const std::exception& e) {
            HCE_ERROR_METHOD_BODY("run","caught exception: ", e.what());
            handle_exception();
        } catch(...) {
            HCE_ERROR_METHOD_BODY("run","caught unknown exception");
            handle_exception();
        }

        // Call suspend handlers when run_() returns early due to 
        // `hce::scheduler::lifecycle::suspend()` being called.
        if(cont) { 
            HCE_HIGH_METHOD_BODY("run","hce::scheduler::config::on_suspend.call()");
            config_->on_suspend.call(*this); 
        }
    } while(cont);

    // call halt handlers
    HCE_HIGH_METHOD_BODY("run","hce::scheduler::config::on_halt.call()");
    config_->on_halt.call(*this);
}

bool hce::scheduler::run_() {
    HCE_MED_METHOD_ENTER("run_");

    // only call function tl_this_scheduler() once to acquire reference to 
    // thread local shared scheduler pointer 
    auto& tl_sch = detail::scheduler::tl_this_scheduler();
    
    // assign thread_local scheduler pointer to this scheduler
    tl_sch = this; 

    // batch of coroutines to evaluate
    std::unique_ptr<scheduler::queue> local_queue(new scheduler::queue);

    // the current time
    hce::chrono::time_point now; 

    // count of operations completed in the current batch
    size_t batch_done_count = 0;

    // Push any remaining coroutines back into the main queue. Lock must be 
    // held before this is called.
    auto cleanup_batch = [&] {
        // reset scheduler batch evaluating count 
        batch_size_ = 0; 

        // update completed operations 
        operations_.completed(batch_done_count);

        // reset batch done operation count
        batch_done_count = 0;

        // push every uncompleted coroutine to the back of the scheduler's 
        // main queue
        while(local_queue->size()) {
            coroutine_queue_->push_back(local_queue->front());
            local_queue->pop_front();
        }
    };

    // acquire the lock
    std::unique_lock<spinlock> lk(lk_);

    // ensure the current exception is reset
    current_exception_ = nullptr;

    // block until no longer suspended
    while(state_ == suspended) { 
        HCE_MED_METHOD_BODY("run_","suspended before run loop");
        resume_block_(lk); 
    }
    
    HCE_MED_METHOD_BODY("run_","entering run loop");

    // If halt_() has been called, return immediately
    if(state_ == ready) {
        state_ = executing; 

        // flag to control evaluation loop
        bool evaluate = true;

        try {
            // Evaluation loop runs fairly continuously. 99.9% of the time 
            // it is expected a scheduler is executing code within this 
            // do-while.
            do {
                // check for any ready timers 
                if(timers_.size()) {
                    // update the current timepoint
                    now = hce::chrono::now();
                    auto it = timers_.begin();
                    auto end = timers_.end();

                    while(it!=end) {
                        // check if a timer is ready to timeout
                        if((*it)->timeout() <= now) {
                            // every ready timer is a done operation
                            ++batch_done_count;
                            /*
                             Handle ready timers before coroutines in case 
                             exceptions occur in user code. Resuming a timer 
                             will push a coroutine handle onto the 
                             `coroutine_queue_` member.
                              */
                            (*it)->resume((void*)1);
                            it = timers_.erase(it);
                        } else {
                            // no remaining ready timers, exit loop early
                            break;
                        }
                    }
                }

                // check for waiting coroutines
                if(coroutine_queue_->size()) {
                    // acquire the current batch of coroutines by trading 
                    // with the scheduler's queue, reducing lock contention 
                    // by collecting the entire batch via a pointer swap
                    std::swap(local_queue, coroutine_queue_);

                    // update API accessible batch count
                    batch_size_ = local_queue->size();

                    // unlock scheduler when running executing coroutines
                    lk.unlock();

                    {
                        size_t count = local_queue->size();

                        // scope local coroutine to ensure std::coroutine_handle 
                        // lifecycle management with RAII semantics
                        coroutine co;

                        // evaluate the batch of coroutines once through
                        while(count) { 
                            // decrement from our initial batch count
                            --count;

                            // get a new task, swapping with the front of the 
                            // task queue
                            co.reset(local_queue->front());
                            
                            // if a handle swap occurred, this will cause the 
                            // std::coroutine_handle to be destroyed
                            local_queue->pop_front();

                            // execute the coroutine
                            co.resume();

                            // check if the coroutine has a handle to manage
                            if(co) {
                                if(!co.done()) {
                                    // locally re-enqueue coroutine 
                                    local_queue->push_back(co.release()); 
                                } else {
                                    // update done operation count
                                    ++batch_done_count;
                                }
                            } // else coroutine was suspended
                        }
                    } // make sure last coroutine is cleaned up before lock

                    // reacquire lock
                    lk.lock(); 
                }

                // cleanup batch results, requeueing coroutines
                cleanup_batch();

                // keep executing if coroutines are available
                if(coroutine_queue_->empty()) {
                    // check if any running timers exist
                    if(timers_.empty()) {
                        // verify run state and block if necessary
                        if(can_evaluate_()) {
                            // wait for more tasks
                            waiting_for_tasks_ = true;
                            tasks_available_cv_.wait(lk);
                        } else {
                            // break out of the evaluation loop because 
                            // execution is halted and no operations remain
                            evaluate=false;
                        }
                    } else {
                        // check the time again
                        now = hce::chrono::now();
                        auto timeout = timers_.front()->timeout();

                        // wait, at a maximum, till the next scheduled
                        // timer timeout. If a timer is ready to timeout
                        // continue to the next iteration.
                        if(timeout > now) {
                            waiting_for_tasks_ = true;
                            tasks_available_cv_.wait_until(lk, timeout);
                        }
                    }
                }
            } while(evaluate); 
        } catch(...) { // catch all other exceptions 
            // reset thread local state in case of uncaught exception
            tl_sch = nullptr; 

            // it is an error in this framework if an exception occurs when 
            // the lock is held, it should only be when executing user 
            // coroutines that this can even occur
            lk.lock();

            current_exception_ = std::current_exception();
            cleanup_batch();

            lk.unlock();

            std::rethrow_exception(current_exception_);
        }
    }

    // restore parent thread_local pointer
    tl_sch = nullptr;

    if(state_ == suspended) {
        HCE_MED_METHOD_BODY("run_","exitted run loop, suspended");
        // reset scheduler state so run() can be called again
        reset_flags_();
        return true;
    } else {
        HCE_MED_METHOD_BODY("run_","exitted run loop, halted");
        halt_notify_();
        return false;
    }
}

void hce::scheduler::suspend_() {
    std::unique_lock<hce::spinlock> lk(lk_);

    if(state_ != halted) { 
        state_ = suspended;

        // wakeup scheduler if necessary from waiting for tasks to force 
        // run() to exit
        tasks_available_notify_();
    }
}

void hce::scheduler::resume_() {
    std::unique_lock<spinlock> lk(lk_);
   
    if(state_ == suspended) { 
        state_ = ready; 
        resume_notify_();
    }
}

void hce::scheduler::halt_() {
    std::unique_lock<hce::spinlock> lk(lk_);

    if(state_ != halted) {
        bool was_running = state_ == executing; 

        // set the scheduler to the permanent halted state
        state_ = halted;

        // resume scheduler if necessary
        resume_notify_();

        if(detail::scheduler::tl_this_scheduler() != this) {
            // wakeup scheduler if necessary
            tasks_available_notify_();
        
            if(was_running) {
                // block until halted
                HCE_MED_METHOD_BODY("halt_","waiting");
                waiting_for_halt_ = true;
                halt_cv_.wait(lk);
            }
        }
    }
}
