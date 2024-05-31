//SPDX-License-Identifier: Apache-2.0
//Author: Blayne Dennis 
#include <thread>
#include <memory>

#include "scheduler.hpp"

hce::scheduler*& hce::detail::tl_this_scheduler() {
    thread_local hce::scheduler* tlts = nullptr;
    return tlts;
}

hce::scheduler*& hce::detail::tl_this_scheduler_redirect() {
    thread_local hce::scheduler* tlts = nullptr;
    return tlts;
}

hce::scheduler& hce::scheduler::global() {
    struct scheduler_manager {
        scheduler_manager() : 
            sch(hce::scheduler::make()),
            thd([&]{ while(sch->run()) { } })
        { }

        ~scheduler_manager() {
            sch->halt();
            thd.join();
        }

        std::shared_ptr<hce::scheduler> sch;
        std::thread thd;
    };

    static scheduler_manager sch_mgr;
    return *(sch_mgr.sch);
}
