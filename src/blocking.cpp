#include "blocking.hpp"
#include "lifecycle.hpp"

hce::blocking::service* hce::blocking::service::instance_ = nullptr;

hce::blocking::service& hce::blocking::service::get() {
    return *(hce::blocking::service::instance_);
}

hce::blocking::service::cache& 
hce::blocking::service::tl_worker_cache_() {
    thread_local hce::blocking::service::cache c;
    return c;
};
