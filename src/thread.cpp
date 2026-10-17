#include "thread.hpp"

hce::thread::local::table_t::table_t() : use_count(0) {
    lookup.fill(0);
}

hce::thread::local::table_t::~table_t() { this->clean(); }

void* hce::thread::local::table_t::cleanup_alloc(size_t sz) { 
    return std::malloc(sz); 
}

void hce::thread::local::table_t::cleanup_dealloc(void* p) { 
    return std::free(p); 
}

hce::thread::local::~local(){ }

hce::thread::local::local() { }

// acquire a the thread local table for a ptr
hce::thread::local::table_t* hce::thread::local::acquire_() {
    std::thread::id id = std::this_thread::get_id();

    std::unique_lock<hce::spinlock> lk(lk_);
    std::unique_ptr<table_t>& table = thread_map_[id];

    // as soon as we have the reference to the local map we don't need the 
    // lock because the table won't move and will only be written to by this 
    // system thread 
    lk.unlock();

    if(!table) [[unlikely]] {
        // we initialize outside the lock
        table.reset(new table_t);
    }
    
    ++(table->use_count);
    return table.get();
}

// inform the service a ptr is releasing
void hce::thread::local::release_(hce::thread::local::table_t* table) {
    if(table->use_count > 1) {
        --(table->use_count);
    } else {
        // cleanup local table from thread map if necessary 
        std::thread::id id = std::this_thread::get_id();
        std::unique_ptr<table_t> table_mem;

        std::lock_guard<hce::spinlock> lk(lk_);
        auto thread_it = thread_map_.find(id);
        table_mem = std::move(thread_it->second); // swap memory
        thread_map_.erase(thread_it); // erase now empty pointer from map

        // allow cleanup operations to happen outside lock during table_mem
        // destructor
    }
}

/**
@brief attempt to set a thread's priority in a system agnostic way 
*/
bool hce::thread::set_priority(std::thread& thr, int priority) {
#if defined(_WIN32) || defined(_WIN64)
    HANDLE handle = static_cast<HANDLE>(thr.native_handle());
    bool success = SetThreadPriority(handle, priority) != 0;
    HCE_ERROR_GUARD(!success, HCE_ERROR_FUNCTION_BODY("hce::thread::set_priority", "Failed to set thread scheduling policy on Window: SetThreadPriority() returned 0"));
    return success;
#elif defined(_POSIX_VERSION)
    pthread_t native = thr.native_handle();
    struct sched_param param;
    int policy;

    if (pthread_getschedparam(native, &policy, &param) != 0) {
        HCE_ERROR_FUNCTION_BODY("hce::thread::set_priority", "Failed to get thread scheduling policy on POSIX: Unable to get thread scheduling parameters");
        return false; 
    }

    if (policy == SCHED_OTHER) {
        // Only adjust priority within SCHED_OTHER's valid range.
        param.sched_priority = priority; // Valid range: implementation-defined.
        bool success = pthread_setschedparam(native, policy, &param) == 0;

        HCE_ERROR_GUARD(!success, HCE_ERROR_FUNCTION_BODY("hce::thread::set_priority", "Failed to get thread scheduling policy on POSIX: Unable to set scheduler parameters"));

        return success;
    }

    HCE_INFO_FUNCTION_BODY("hce::thread::set_priority", "Count not set thread scheduling policy on POSIX: Unsupported policy for non-superuser adjustment");
    return false;
#else
    HCE_WARNING_FUNCTION_BODY("hce::thread::set_priority", "Failed to set thread scheduling policy: Unsupported platform");
    return false;
#endif 
}
