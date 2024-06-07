#include "block.hpp"

#ifndef HCEMINBLOCKPROCS
#define HCEMINBLOCKPROCS 1
#endif 

hce::blocking::blocking() :
        min_worker_cnt_(HCEMINBLOCKPROCS ? HCEMINBLOCKPROCS : 1),
        worker_cnt_(min_worker_cnt_),
        workers_(min_worker_cnt_) { 
    // create block workers
    for(auto& w : workers_) {
        w = std::unique_ptr<hce::blocking::worker>(
            new hce::blocking::worker()); 
    }
}

bool& hce::blocking::worker::tl_is_block() {
    thread_local bool iw = false;
    return iw;
}
    
hce::blocking& hce::blocking::instance() {
    static hce::blocking blkr;
    return blkr;
}
