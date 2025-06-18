#include "blocking.hpp"

hce::blocking::manager::manager() :
    blocking_(
        new hce::blocking(
            hce::config::blocking::reusable_block_worker_cache_size()))
{
    HCE_HIGH_CONSTRUCTOR();
}

hce::blocking::manager::~manager() { HCE_HIGH_DESTRUCTOR(); }

std::string hce::blocking::manager::info_name() { 
    return ("hce::blocking::manager"); 
}

std::string hce::blocking::manager::name() const { 
    return hce::blocking::manager::info_name(); 
}

hce::blocking& hce::blocking::manager::blocking() {
    return *blocking_;
}

hce::blocking::blocking(size_t worker_cache_size) :
    worker_active_count_(0),
    worker_cache_(worker_cache_size)
{ 
    HCE_MED_CONSTRUCTOR();
}

hce::blocking::~blocking() { HCE_MED_DESTRUCTOR(); }
std::string hce::blocking::info_name() { return ("hce::blocking"); }
std::string hce::blocking::name() const { return hce::blocking::info_name(); }

size_t hce::blocking::worker_cache_size() const { 
    HCE_LOW_METHOD_BODY("worker_cache_size",worker_cache_.size());
    std::lock_guard<hce::spinlock> lk(lk_);
    return worker_cache_.size();
}

void hce::blocking::worker_cache_size(size_t new_size)  { 
    HCE_LOW_METHOD_ENTER("worker_cache_size",new_size);
    std::lock_guard<hce::spinlock> lk(lk_);

    hce::circular_buffer<std::unique_ptr<worker>> new_worker_cache(new_size);

    while(worker_cache_.used() && new_worker_cache.remaining()) {
        new_worker_cache.push(std::move(worker_cache_.front()));
        worker_cache_.pop();
    }

    worker_cache_ = std::move(new_worker_cache);
}

size_t hce::blocking::worker_count() const {
    size_t c;

    {
        std::lock_guard<hce::spinlock> lk(lk_);
        c = worker_active_count_ + worker_cache_.used();
    }

    HCE_LOW_METHOD_BODY("worker_count",c);
    return c; 
}

void hce::blocking::clear_worker_cache() {
    HCE_LOW_METHOD_ENTER("clear");

    std::lock_guard<spinlock> lk(lk_);
    while(worker_cache_.used()) {
        worker_cache_.pop();
    }
}

hce::blocking::worker::worker() : 
    thd_(hce::blocking::worker::run_, &operations_) 
{ 
    HCE_LOW_CONSTRUCTOR();
}

hce::blocking::worker::~worker() { 
    HCE_LOW_DESTRUCTOR(); 
    operations_.close();
    thd_.join();
}

std::string hce::blocking::worker::info_name() { return "hce::blocking::worker"; }
std::string hce::blocking::worker::name() const { return hce::blocking::worker::info_name(); }

void hce::blocking::worker::schedule(std::unique_ptr<hce::thunk>&& operation) { 
    operations_.push_back(std::move(operation));
}

void hce::blocking::worker::run_(
    hce::synchronized_list<std::unique_ptr<hce::thunk>>* operations) 
{
    std::unique_ptr<hce::thunk> operation;

    while(operations->pop(operation)) [[likely]] {
        // execute operations sequentially until recv() returns false
        (*operation)();
    }
}

// retrieve a worker thread from the service to execute blocking operations on
std::unique_ptr<hce::blocking::worker> hce::blocking::checkout_worker_() {
    // attempt to pull from the thread_local cache first
    std::unique_ptr<hce::blocking::worker> w;

    std::unique_lock<hce::spinlock> lk(lk_);
    ++worker_active_count_; // update checked out thread count
    
    // check if we have any workers in reserve
    if(worker_cache_.empty()) [[unlikely]] {
        // need to start a new worker thread but don't need to hold the lock 
        lk.unlock();

        // as a fallback generate a new worker thread 
        w.reset(new worker());
        HCE_TRACE_METHOD_BODY("checkout_worker_","allocated ",w.get());
    } else [[likely]] {
        // get the first available worker
        w = std::move(worker_cache_.front());
        worker_cache_.pop();
        lk.unlock();

        HCE_TRACE_METHOD_BODY("checkout_worker_","reused ",w.get());
    }

    return w;
}

// return a worker to the service when blocking operation is completed
void hce::blocking::checkin_worker_(std::unique_ptr<hce::blocking::worker>&& w) {
    std::lock_guard<hce::spinlock> lk(lk_);
    --worker_active_count_; // update checked out thread count
    
    if(worker_cache_.full()) {
        HCE_TRACE_METHOD_BODY("checkin_worker_","discarded ",w.get());
    } else { 
        HCE_TRACE_METHOD_BODY("checkin_worker_","cached ",w.get());
        worker_cache_.push(std::move(w)); // reuse worker
    }
}
