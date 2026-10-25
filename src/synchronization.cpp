#include "synchronization.hpp"

hce::detail::sync::done::done() :
    hce::scheduler::reschedule<
        hce::awaitable::lockable<
            hce::lockfree,
            hce::awt_interface<void>>>(
                lf_,
                hce::awaitable::await::policy::defer,
                hce::awaitable::resume::policy::lock)
{ }

bool hce::detail::sync::done::on_ready() { return true; }
void hce::detail::sync::done::on_resume(void* m) { }
