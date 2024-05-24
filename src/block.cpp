#include "block.hpp"

#ifndef HCEMINBLOCKPROCS
#define HCEMINBLOCKPROCS 1
#endif 

hce::blocking::blocker::blocker() :
        min_worker_cnt_(HCEMINBLOCKPROCS ? HCEMINBLOCKPROCS : 1),
        worker_cnt_(min_worker_cnt_),
        workers_(min_worker_cnt_) { 
    // create block workers
    for(auto& w : workers_) {
        w = std::unique_ptr<hce::blocking::worker>(new worker(this)); 
    }
}

bool& hce::blocking::worker::tl_is_wait() {
    thread_local bool iw = false;
    return iw;
}
    
hce::blocking::blocker& hce::blocking::instance() {
    static hce::blocking::blocker blkr;
    return blkr;
}
