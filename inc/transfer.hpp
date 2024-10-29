//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#ifndef __HERMES_COROUTINE_ENGINE_SEND__
#define __HERMES_COROUTINE_ENGINE_SEND__

#include <exception>
#include <string>
#include <sstream>
#include <memory>

#include "utility.hpp"
#include "logging.hpp"
#include "atomic.hpp"
#include "coroutine.hpp"
#include "scheduler.hpp"

namespace hce {

/**
 @brief a lightweight mechanism to send a single value to an awaitable receiver
 */
template <typename T, typename LOCK=hce::spinlock>
struct transfer : public printable {
    struct cannot_call_op_twice : public std::exception {
        cannot_call_op_twice(const char* op, void* s) :
            estr([&]() -> std::string {
                std::stringstream ss;
                ss << "Error: "
                   << (hce::printable*)s 
                   << " had " 
                   << transfer<T>::info_name() + "::" + op + " called twice";
                return ss.str();
            }()) 
        { }

        inline const char* what() const noexcept { return estr.c_str(); }

    private:
        const std::string estr;
    };

    transfer() : 
        awaitable_(new awaitable(lk_)),
        awaitable_in_(awaitable_) // a copy of awaitable with no memory management
    { }

    transfer(const transfer<T>&) = delete;

    transfer(transfer<T>&& rhs) {
        awaitable_ = rhs.awaitable_;
        awaitable_in_ = rhs.awaitable_in_;
        rhs.awaitable_ = nullptr;
        rhs.awaitable_in_ = nullptr;
    }

    ~transfer() { if(awaitable_) [[unlikely]] { delete awaitable_; } }

    transfer<T>& operator=(const transfer<T>&) = delete;

    transfer<T>& operator=(transfer<T>&& rhs) {
        std::swap(awaitable_, rhs.awaitable_);
        std::swap(awaitable_in_, rhs.awaitable_in_);
        return *this;
    }

    static inline std::string info_name() { 
        return type::templatize<T>("hce::transfer"); 
    }

    inline std::string name() const { return transfer<T>::info_name(); }

    inline std::string content() {
        std::stringstream ss;
        ss << awaitable_;
        return ss.str();
    }

    /**
     @brief send the value

     This operation is non-blocking.

     @param t the value
     */
    inline void send(const T& t) {
        std::lock_guard<spinlock> lk(lk_);

        if(!awaitable_in_) [[unlikely]] { 
            throw cannot_call_op_twice("send", this); 
        }

        hce::send_pair<T> sp{(void*)&t,false};
        awaitable_in_->resume(&sp);
        awaitable_in_ = nullptr;
    }

    /**
     @brief send the value

     This operation is non-blocking.

     @param t the value
     */
    inline void send(T&& t) {
        std::lock_guard<spinlock> lk(lk_);

        if(!awaitable_in_) [[unlikely]] { 
            throw cannot_call_op_twice("send", this); 
        }

        hce::send_pair<T> sp{&t,true};
        awaitable_in_->resume(&sp);
        awaitable_in_ = nullptr;
    }
   
    /**
     Hint: the returned awaitable can be passed to a coroutine.

     @return an awaitable to receive the sent value
     */
    inline hce::awt<std::unique_ptr<T>> recv() {
        if(!awaitable_) { throw cannot_call_op_twice("recv", this); }
        auto awt = hce::awt<std::unique_ptr<T>>::make(awaitable_);
        awaitable_ = nullptr;
        return awt;
    }

private:
    struct awaitable : 
        public hce::scheduler::reschedule<
            hce::awaitable::lockable<
                hce::awt_interface<std::unique_ptr<T>>,
                LOCK>>
    {
        awaitable(LOCK& lk) :
            hce::scheduler::reschedule<
                hce::awaitable::lockable<
                    hce::awt_interface<std::unique_ptr<T>>,
                    LOCK>>(
                        lk,
                        hce::awaitable::await::policy::defer,
                        hce::awaitable::resume::policy::adopt),
            t_(nullptr)
        {
            HCE_TRACE_CONSTRUCTOR();
        }

        virtual ~awaitable() { HCE_TRACE_DESTRUCTOR(); }

        static inline std::string info_name() { 
            return hce::transfer<T>::info_name() + "::awaitable"; 
        }

        inline std::string name() const { return awaitable::info_name(); }

        inline std::string content() {
            std::stringstream ss;
            ss << t_;
            return ss.str();
        }

        inline bool on_ready() { return (bool)t_; }

        inline void on_resume(void* m) {
            t_.reset(new T);
            ((hce::send_pair<T>*)m)->send(t_.get()); 
        }

        inline std::unique_ptr<T> get_result() { return std::move(t_); }

    private:
        bool ready_;
        std::unique_ptr<T> t_;
    };

    LOCK lk_;
    awaitable* awaitable_;
    awaitable* awaitable_in_;
};

}

#endif
