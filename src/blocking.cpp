#include "blocking.hpp"

hce::blocking::service& hce::blocking::service::get() {
    static hce::blocking::service s;
    return s;
}

hce::blocking::service::cache& 
hce::blocking::service::tl_worker_cache_() {
    thread_local hce::blocking::service::cache c;
    return c;
};
