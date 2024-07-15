#include "block.hpp"

#ifndef HCEMINBLOCKPROCS
#define HCEMINBLOCKPROCS 1
#endif 

// create no workers initially, but reuse them as they are checked back 
// in as long as the stored workers are less than the min_worker_cnt_
hce::scheduler::blocking::blocking() :
    min_worker_cnt_(HCEMINBLOCKPROCS ? HCEMINBLOCKPROCS : 1)
{ }

bool& hce::blocking::worker::tl_is_block() {
    thread_local bool iw = false;
    return iw;
}
    
hce::blocking& hce::blocking::instance() {
    static hce::blocking blkr;
    return blkr;
}
