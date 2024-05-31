#include "block.hpp"

#ifndef HCEMINBLOCKPROCS
#define HCEMINBLOCKPROCS 1
#endif 

hce::detail::blocking::blocker::blocker() :
        min_worker_cnt_(HCEMINBLOCKPROCS ? HCEMINBLOCKPROCS : 1),
        worker_cnt_(min_worker_cnt_),
        workers_(min_worker_cnt_) { 
    // create block workers
    for(auto& w : workers_) {
        w = std::unique_ptr<hce::detail::blocking::blocker::worker>(
            new hce::detail::blocking::blocker::worker()); 
    }
}

bool& hce::detail::blocking::blocker::worker::tl_is_block() {
    thread_local bool iw = false;
    return iw;
}
    
hce::detail::blocking::blocker& hce::detail::blocking::blocker::instance() {
    static hce::detail::blocking::blocker blkr;
    return blkr;
}
