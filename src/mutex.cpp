#include "mutex.hpp"

hce::mutex::already_unlocked_exception::already_unlocked_exception(mutex* m) :
    estr([&]() -> std::string {
        std::stringstream ss;
        ss << "cannot unlock already unlocked " << m->to_string();
        return ss.str();
    }()) 
{ }

const char* hce::mutex::already_unlocked_exception::what() const noexcept { 
    HCE_ERROR_LOG("%s",estr.c_str());
    return estr.c_str(); 
}

hce::mutex::mutex(hce::pool_allocator<hce::awaitable::interface*> alloc) :
    acquired_(false),
    blocked_queue_(alloc)
{ 
    HCE_MIN_CONSTRUCTOR(); 
}

hce::mutex::~mutex() { 
    HCE_MIN_DESTRUCTOR(); 

    if(blocked_queue_.size()) [[unlikely]] {
        HCE_FATAL_METHOD_BODY("hce::mutex::~mutex()","mutex destroyed while operations are waiting on it, causing deadlock");
        std::terminate();
    }
}

std::string hce::mutex::info_name() { return "hce::mutex"; }
std::string hce::mutex::name() const { return mutex::info_name(); }

hce::awt<void> hce::mutex::lock() {
    HCE_MIN_METHOD_ENTER("lock");
    return hce::awt<void>::make<hce::mutex::acquire>(this);
}

bool hce::mutex::try_lock() {
    HCE_MIN_METHOD_ENTER("try_lock");

    std::unique_lock<hce::spinlock> lk(slk_);

    if(acquired_) [[unlikely]] {
        return false;
    } else [[likely]] { 
        acquired_ = true; 
        return true;
    }
}

void hce::mutex::unlock() {
    HCE_MIN_METHOD_ENTER("unlock");

    std::unique_lock<hce::spinlock> lk(slk_);

    if(acquired_) [[likely]] {
        if(blocked_queue_.size()) {
            blocked_queue_.front()->resume(nullptr);
            blocked_queue_.pop();
        } else {
            acquired_ = false;
        }
    } else [[unlikely]] { 
        throw already_unlocked_exception(this); 
    }
}

hce::mutex::acquire::acquire(hce::mutex* parent) : 
    hce::scheduler::reschedule<
        hce::awaitable::lockable<
            hce::spinlock,
            hce::awt<void>::interface>>(
                parent->slk_,
                hce::awaitable::await::policy::defer_lock,
                hce::awaitable::resumed::policy::unlocked,
                hce::awaitable::resume::policy::lock_responsible),
    parent_(parent) 
{ }

void hce::mutex::acquire::on_ready() { 
    if(parent_->lock_or_enqueue_blocked_(this)) {
        this->set_ready();
    }
}

void hce::mutex::acquire::on_resume(void* m) { }

bool hce::mutex::lock_or_enqueue_blocked_(hce::mutex::acquire* acq) {
    if(acquired_) [[unlikely]] {
        blocked_queue_.push_back(static_cast<hce::awaitable::interface*>(acq));
        return false;
    } else [[likely]] { 
        acquired_ = true; 
        return true;
    }
}
