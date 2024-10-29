//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#include <string>
#include <sstream>
#include <thread>

#include "loguru.hpp"
#include "atomic.hpp"
#include "coroutine.hpp"
#include "scheduler.hpp"
#include "transfer.hpp"

// test, only uncomment for development of this library
//#include "dev_print.hpp"

hce::coroutine*& hce::detail::coroutine::tl_this_coroutine() {
    thread_local hce::coroutine* tltc = nullptr;
    return tltc;
}

hce::detail::coroutine::this_thread* 
hce::detail::coroutine::this_thread::get() {
    thread_local hce::detail::coroutine::this_thread tlatt;
    return &tlatt;
}

void hce::detail::coroutine::coroutine_did_not_co_await(void* awt) {
    std::stringstream ss;
    ss << "hce::coroutine[0x" 
       << (void*)(hce::coroutine::local().address())
       << "] did not call co_await on an hce::awaitable[0x"
       << awt 
       << "]";
    HCE_ERROR_LOG("%s",ss.str().c_str());
}

/*
 A new coroutine is needed to handle the business logic of notifying the 
 lifecycle manager when an awaitable is done because the lifecycle manager is 
 lazy about actually joining with registered awaitables to protect against 
 memory leaks.
 */
hce::co<void> detach_co_(
        hce::awaitable::interface* orig, 
        hce::awt<std::unique_ptr<size_t>> key_awt)
{
    // await the original awaitable in this coroutine
    co_await hce::awt<void>::make(orig);

    // receive the key for this coroutine's own awaitable acquired in detach()
    // and notify the lifecycle manager it can be awaited
    hce::scheduler::lifecycle::manager::instance().done_await(
        *(co_await std::move(key_awt)));
}

void hce::awaitable::detach() {
    if(unfinalized_()) {
        // get this awaitable's interface
        hce::awaitable::interface* i = release();

        // an object to transfer the key through
        hce::transfer<size_t> key_transfer;

        // Launch and register the new coroutine with the lifecycle manager to 
        // make sure it gets awaited. The returned key needs to be transferred 
        // to the running coroutine.
        size_t key = hce::scheduler::lifecycle::manager::instance()
            .register_await(hce::scheduler::get().schedule(
                detach_co_(i, key_transfer.recv())).release());

        // send the coroutine the key to its own awaitable interface
        key_transfer.send(key);
    }
}
