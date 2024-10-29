//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#ifndef __HERMES_COROUTINE_ENGINE_CHANNEL__
#define __HERMES_COROUTINE_ENGINE_CHANNEL__

// c++
#include <typeinfo>
#include <mutex>

// local
#include "utility.hpp"
#include "logging.hpp"
#include "atomic.hpp"
#include "circular_buffer.hpp"
#include "queue.hpp"
#include "coroutine.hpp"
#include "scheduler.hpp"

namespace hce {

/// result of a channel try operation
enum result {
    closed = 0, /// channel is closed
    failure, /// channel operation failed
    success /// channel operation succeeded
};

namespace detail {
namespace channel {

/// interface for a channel implementation
template <typename T>
struct interface : public printable {
    virtual ~interface(){}

    /// type_info of the implementation
    virtual const std::type_info& type_info() const = 0;

    /**
     @brief maximum capacity of stored value cache 

    Typical implementation expectations:
    >0 == cache of a specific maximum size
    0 == cache of no size (direct point to point data transfer)
    <0 == cache of unlimited size
     */
    virtual int size() const = 0;

    /// current size of the stored value cache
    virtual int used() const = 0;

    /// return true if implementation is closed, else false
    virtual bool closed() const = 0;

    /// close the implementation, causing future sends/receives to fail
    virtual void close() = 0;

    /// send a lvalue copy, awaitable returning true on success, else false
    virtual hce::awt<bool> send(const T& t) = 0;

    /// send a rvalue copy, awaitable returning true on success, else false
    virtual hce::awt<bool> send(T&& t) = 0;

    /// receive a value, awaitable returning true on success, else false
    virtual hce::awt<bool> recv(T& t) = 0;

    /// attempt to send a lvalue copy
    virtual hce::yield<hce::result> try_send(const T& t) = 0;

    /// attempt to send a rvalue copy
    virtual hce::yield<hce::result> try_send(T&& t) = 0;

    /// attempt receive a value
    virtual hce::yield<hce::result> try_recv(T& t) = 0;
};

/// unbuffered interface implementation
template <typename T, typename LOCK>
struct unbuffered : public interface<T> {
    typedef T value_type;
    static constexpr size_t pool_default_size_limit = 
        hce::queue<T>::pool_default_size_limit;

    unbuffered(size_t pool_size_limit = pool_default_size_limit) : 
        closed_flag_(false),
        parked_send_(pool_size_limit),
        parked_recv_(pool_size_limit)
    { 
        HCE_LOW_CONSTRUCTOR(); 
    }
    
    virtual ~unbuffered(){ HCE_LOW_DESTRUCTOR(); }

    static inline std::string info_name() { 
        return type::templatize<T,LOCK>("hce::unbuffered"); 
    }

    inline std::string name() const { return type::name<unbuffered<T,LOCK>>(); }

    inline const std::type_info& type_info() const {
        return typeid(unbuffered); 
    }

    inline int size() const { 
        HCE_MIN_METHOD_ENTER("size");
        return 0; 
    }

    inline int used() const { 
        HCE_MIN_METHOD_ENTER("used");
        return 0; 
    }

    inline bool closed() const {
        HCE_MIN_METHOD_ENTER("closed");

        std::unique_lock<LOCK> lk(lk_);
        return closed_flag_;
    }

    inline void close() {
        HCE_LOW_METHOD_ENTER("close");

        std::lock_guard<LOCK> lk(lk_);
        if(!closed_flag_) [[likely]] {
            closed_flag_ = true;

            while(parked_send_.size()) { 
                HCE_TRACE_METHOD_BODY("close","closing parked send:",parked_send_.front());
                parked_send_.front()->resume(nullptr); 
                parked_send_.pop();
            }

            while(parked_recv_.size()) { 
                HCE_TRACE_METHOD_BODY("close","closing parked recv:",parked_recv_.front());
                parked_recv_.front()->resume(nullptr); 
                parked_recv_.pop();
            }
        }
    }

    inline awt<bool> send(const T& t) {
        HCE_LOW_METHOD_ENTER("send",(void*)&t);
        return send_((void*)&t, false);
    }

    inline awt<bool> send(T&& t) {
        HCE_LOW_METHOD_ENTER("send",(void*)&t);
        return send_((void*)&t, true);
    }

    inline awt<bool> recv(T& t) {
        HCE_LOW_METHOD_ENTER("recv",(void*)&t);
        return recv_((void*)&t);
    }

    inline hce::yield<hce::result> try_send(const T& t) {
        HCE_LOW_METHOD_ENTER("try_send",(void*)&t);
        return try_send_((void*)&t, false);
    }

    inline hce::yield<hce::result> try_send(T&& t) {
        HCE_LOW_METHOD_ENTER("try_send",(void*)&t);
        return try_send_((void*)&t, true);
    }

    inline hce::yield<hce::result> try_recv(T& t) {
        HCE_LOW_METHOD_ENTER("try_recv",(void*)&t);
        return try_recv_((void*)&t);
    }

private:
    inline hce::awt<bool> send_(void* s, bool is_rvalue) {
        struct send_interface : 
            public hce::scheduler::reschedule<
                hce::awaitable::lockable<
                    hce::awt_interface<bool>,
                    LOCK>>
        {
            send_interface(
                    std::unique_lock<LOCK>& lk, 
                    bool ready, 
                    bool success, 
                    hce::send_pair<T> sp) :
                hce::scheduler::reschedule<
                    hce::awaitable::lockable<
                        hce::awt_interface<bool>,
                        LOCK>>(
                            *(lk.mutex()),
                            hce::awaitable::await::policy::adopt,
                            hce::awaitable::resume::policy::no_lock),
                ready_(ready),
                success_(success),
                sp_(sp)
            { 
                lk.release();
            }

            inline bool on_ready() { return ready_; }

            inline void on_resume(void* m) {
                if(m) [[likely]] {
                    sp_.send(m);
                    success_ = true;
                }
                else{ success_ = false; }
            }

            inline bool get_result() { return success_; }

        private:
            bool ready_;
            bool success_;
            hce::send_pair<T> sp_;
        };

        std::unique_lock<LOCK> lk(lk_);

        if(closed_flag_) [[unlikely]] {
            HCE_TRACE_METHOD_BODY("send_","closed");
            return hce::awt<bool>::make(new send_interface(lk, true, false, hce::send_pair<T>()));
        } else if(parked_recv_.size()) [[likely]] {
            HCE_TRACE_METHOD_BODY("send_","resume receiver");
            hce::send_pair<T> sp{s,false};
            parked_recv_.front()->resume((void*)(&sp));
            parked_recv_.pop();

            // return an awaitable which immediately returns true
            return hce::awt<bool>::make(new send_interface(lk, true, true, hce::send_pair<T>()));
        } else [[unlikely]] {
            HCE_TRACE_METHOD_BODY("send_","block for receiver");
            auto ai = new send_interface(lk, false, true, hce::send_pair<T>(s, is_rvalue ));
            parked_send_.push_back(ai);
            return hce::awt<bool>::make(ai);
        }
    }

    /// stackless `co_await`able recv operation
    inline hce::awt<bool> recv_(void* r) {
        struct recv_interface : 
            public hce::scheduler::reschedule<
                hce::awaitable::lockable<
                    hce::awt<bool>::interface,
                    LOCK>>
        {
            recv_interface(std::unique_lock<LOCK>& lk, bool ready, bool success, void* r) :
                hce::scheduler::reschedule<
                    hce::awaitable::lockable<
                        hce::awt<bool>::interface,
                        LOCK>>(
                            *(lk.mutex()), 
                            hce::awaitable::await::policy::adopt,
                            hce::awaitable::resume::policy::no_lock),
                ready_(ready),
                success_(success),
                r_(r)
            { 
                lk.release();
            }

            inline bool on_ready() { return ready_; }

            inline void on_resume(void* m) {
                if(m) [[likely]] {
                    ((hce::send_pair<T>*)m)->send(r_);
                    success_ = true;
                }
                else { success_ = false; }
            }

            inline bool get_result() { return success_; }

            bool ready_;
            bool success_;
            void* r_; // pointer to receiver memory
        };

        std::unique_lock<LOCK> lk(lk_);

        if(closed_flag_) [[unlikely]] { 
            HCE_TRACE_METHOD_BODY("recv_","closed");
            return hce::awt<bool>::make(new recv_interface(lk, true, false, nullptr));
        } else if(parked_send_.size()) [[likely]] {
            HCE_TRACE_METHOD_BODY("recv_","resume sender");
            parked_send_.front()->resume(r);
            parked_send_.pop();

            // return an awaitable which immediately returns true
            return hce::awt<bool>::make(new recv_interface(lk, true, true, nullptr));
        } else [[unlikely]] {
            HCE_TRACE_METHOD_BODY("recv_","block for sender");
            auto ai = new recv_interface(lk, false, true, r);
            parked_recv_.push_back(ai);
            return hce::awt<bool>::make(ai);
        }
    }
    
    inline hce::yield<hce::result> try_send_(void* s, bool is_rvalue) {
        std::unique_lock<LOCK> lk(lk_);

        if(closed_flag_) [[unlikely]] { 
            return { hce::result::closed }; 
        } else if(parked_recv_.size()) [[likely]] {
            hce::send_pair<T> sp{s,is_rvalue};
            parked_recv_.front()->resume((void*)(&sp));
            parked_recv_.pop();

            // return an awaitable which immediately returns true
            return { hce::result::success }; 
        } else [[unlikely]] { return { hce::result::failure }; }
    }
    
    inline hce::yield<hce::result> try_recv_(void* r) {
        std::unique_lock<LOCK> lk(lk_);

        if(closed_flag_) [[unlikely]] { 
            return { hce::result::closed }; 
        } else if(parked_send_.size()) [[likely]] {
            parked_send_.front()->resume(r);

            // return an awaitable which immediately returns true
            return { hce::result::success }; 
        } else [[unlikely]] { return { hce::result::failure }; }
    }

    mutable LOCK lk_;
    bool closed_flag_;
    hce::queue<awt<bool>::interface*> parked_send_;
    hce::queue<awt<bool>::interface*> parked_recv_;
};

/// buffered interface implementation
template <typename T, typename LOCK>
struct buffered : public interface<T> {
    typedef T value_type;
    static constexpr size_t pool_default_size_limit = 
        hce::queue<T>::pool_default_size_limit;

    buffered(int sz, size_t pool_size_limit = pool_default_size_limit) : 
        closed_flag_(false),
        buf_(sz ? (size_t)sz : (size_t)1),
        parked_send_(pool_size_limit),
        parked_recv_(pool_size_limit)
    { 
        HCE_LOW_CONSTRUCTOR();
    }

    virtual ~buffered(){ HCE_LOW_DESTRUCTOR(); }

    static inline std::string info_name() { 
        return type::templatize<T,LOCK>("hce::buffered"); 
    }

    inline std::string name() const { return type::name<buffered<T,LOCK>>(); }

    inline const std::type_info& type_info() const {
        return typeid(buffered); 
    }

    inline int size() const {
        HCE_MIN_METHOD_ENTER("size");

        std::lock_guard<LOCK> lk(lk_);
        return (int)buf_.size();
    }

    inline int used() const {
        HCE_MIN_METHOD_ENTER("used");

        std::lock_guard<LOCK> lk(lk_);
        return (int)buf_.used();
    }

    inline bool closed() const {
        HCE_MIN_METHOD_ENTER("closed");

        std::lock_guard<LOCK> lk(lk_);
        return closed_flag_;
    }

    inline void close() {
        HCE_LOW_METHOD_ENTER("close");

        std::lock_guard<LOCK> lk(lk_);

        if(!closed_flag_) [[unlikely]] {
            closed_flag_ = true;

            while(parked_send_.size()) { 
                HCE_TRACE_METHOD_BODY("close","closing parked send:",parked_send_.front());
                parked_send_.front()->resume(nullptr); 
                parked_send_.pop();
            }

            while(parked_recv_.size()) { 
                HCE_TRACE_METHOD_BODY("close","closing parked recv:",parked_recv_.front());
                parked_recv_.front()->resume(nullptr); 
                parked_recv_.pop();
            }
        }
    }

    inline awt<bool> send(const T& t) {
        HCE_LOW_METHOD_ENTER("send",(void*)&t);
        return send_((void*)&t, false);
    }

    inline awt<bool> send(T&& t) {
        HCE_LOW_METHOD_ENTER("send",(void*)&t);
        return send_((void*)&t, true);
    }

    /**
     Buffered receives will succeed, even if the channel is closed, as long 
     as values are available for retrieval from the internal buffer.
     */
    inline awt<bool> recv(T& t) {
        HCE_LOW_METHOD_ENTER("recv",(void*)&t);
        return recv_((void*)&t);
    }

    inline hce::yield<hce::result> try_send(const T& t) {
        HCE_LOW_METHOD_ENTER("try_send",(void*)&t);
        return try_send_((void*)&t, false);
    }

    inline hce::yield<hce::result> try_send(T&& t) {
        HCE_LOW_METHOD_ENTER("try_send",(void*)&t);
        return try_send_((void*)&t, true);
    }

    /**
     Buffered receives will succeed, even if the channel is closed, as long 
     as values are available for retrieval from the internal buffer.
     */
    inline hce::yield<hce::result> try_recv(T& t) {
        HCE_LOW_METHOD_ENTER("try_recv",(void*)&t);
        return try_recv_((void*)&t);
    }

private:
    inline hce::awt<bool> send_(void* s, bool is_rvalue) {
        HCE_TRACE_METHOD_ENTER("send_",s,is_rvalue);

        struct send_interface : 
            public hce::scheduler::reschedule<
                hce::awaitable::lockable<
                    hce::awt<bool>::interface, 
                    LOCK>>
        {
            send_interface(
                    std::unique_lock<LOCK>& lk, 
                    bool ready, 
                    bool success, 
                    hce::send_pair<T> sp = hce::send_pair<T>()) :
                hce::scheduler::reschedule<
                    hce::awaitable::lockable<
                        hce::awt<bool>::interface, 
                        LOCK>>(
                            *(lk.mutex()),
                            hce::awaitable::await::policy::adopt,
                            hce::awaitable::resume::policy::no_lock),
                ready_(ready),
                success_(success),
                sp_(sp)
            { 
                lk.release();
            }

            inline bool on_ready() { return ready_; }

            inline void on_resume(void* m) {
                if(m) [[likely]] {
                    sp_.send(m);
                    success_ = true;
                }
                else { success_ = false; }
            }

            inline bool get_result() { return success_; }

            bool ready_;
            bool success_;
            hce::send_pair<T> sp_; 
        };

        std::unique_lock<LOCK> lk(lk_);

        if(closed_flag_) [[unlikely]] {
            HCE_TRACE_METHOD_BODY("send_","closed");
            return hce::awt<bool>::make(new send_interface(lk, true, false));
        } else if(buf_.full()) [[unlikely]] {
            HCE_TRACE_METHOD_BODY("send_","blocked");
            auto ai = new send_interface(lk, false, true, { s, is_rvalue});
            parked_send_.push_back(ai);
            return hce::awt<bool>::make(ai);
        } else [[likely]] {
            HCE_TRACE_METHOD_BODY("send_","done");
            hce::send_pair<T> sp{s,is_rvalue};
            sp.send((void*)&buf_);

            if(parked_recv_.size()) [[unlikely]] {
                parked_recv_.front()->resume((void*)&buf_);
                parked_recv_.pop();
            }

            // return an awaitable which immediately returns true
            return hce::awt<bool>::make(new send_interface(lk, true, true));
        }
    }

    /// stackless `co_await`able recv operation
    inline hce::awt<bool> recv_(void* r) {
        HCE_TRACE_METHOD_ENTER("recv_",r);

        struct recv_interface : 
            public hce::scheduler::reschedule<
                hce::awaitable::lockable<
                    hce::awt<bool>::interface,
                    LOCK>>
        {
            recv_interface(
                    std::unique_lock<LOCK>& lk, 
                    bool owned,
                    bool ready, 
                    bool success, 
                    void* r) :
                hce::scheduler::reschedule<
                    hce::awaitable::lockable<
                        hce::awt<bool>::interface,
                        LOCK>>(
                            *(lk.mutex()),
                            hce::awaitable::await::policy::adopt,
                            hce::awaitable::resume::policy::no_lock),
                ready_(ready),
                success_(success),
                r_(r)
            { 
                lk.release();
            }

            inline bool on_ready() { return ready_; }

            inline void on_resume(void* m) {
                if(m) [[likely]] {
                    auto b = (circular_buffer<T>*)m;
                    *((T*)r_) = std::move(b->front());
                    b->pop();
                    success_ = true;
                }
                else { success_ = false; }
            }

            inline bool get_result() { return success_; }

            bool ready_;
            bool success_;
            void* r_; // pointer to receiver memory
        };

        std::unique_lock<LOCK> lk(lk_);

        if(buf_.empty()) [[unlikely]] {
            if(closed_flag_ ) [[unlikely]] {
                HCE_TRACE_METHOD_BODY("recv_","closed");
                return hce::awt<bool>::make(new recv_interface(lk, true, true, false, nullptr));
            } else [[likely]] {
                HCE_TRACE_METHOD_BODY("recv_","blocked");
                auto ai = new recv_interface(lk, true, false, true, r);
                parked_recv_.push_back(ai);
                return hce::awt<bool>::make(ai);
            }
        } else [[likely]] {
            HCE_TRACE_METHOD_BODY("recv_","done");
            *((T*)r) = std::move(buf_.front());
            buf_.pop();

            if(parked_send_.size()) [[unlikely]] {
                parked_send_.front()->resume((void*)&buf_);
                parked_send_.pop();
            }

            // return an awaitable which immediately returns true
            return hce::awt<bool>::make(new recv_interface(lk, false, true, true, nullptr));
        }
    }
    
    inline hce::yield<hce::result> try_send_(void* s, bool is_rvalue) {
        HCE_TRACE_METHOD_ENTER("try_send_",s,is_rvalue);

        std::unique_lock<LOCK> lk(lk_);

        if(closed_flag_) [[unlikely]] { 
            HCE_TRACE_METHOD_BODY("try_send_","closed");
            return { hce::result::closed }; 
        } else if(!buf_.full()) [[likely]] {
            HCE_TRACE_METHOD_BODY("try_send_","done");
            hce::send_pair<T> sp(s,is_rvalue);
            sp.send((void*)&buf_);

            if(parked_recv_.size()) [[unlikely]] {
                parked_recv_.front()->resume((void*)&buf_);
                parked_recv_.pop();
            }

            // return an awaitable which immediately returns true
            return { hce::result::success }; 
        } else [[unlikely]] { 
            HCE_TRACE_METHOD_BODY("try_send_","failed");
            return { hce::result::failure }; 
        }
    }
    
    inline hce::yield<hce::result> try_recv_(void* r) {
        HCE_TRACE_METHOD_ENTER("try_recv_",r);

        std::unique_lock<LOCK> lk(lk_);

        if(buf_.empty()) [[unlikely]] {
            if(closed_flag_) [[unlikely]] { 
                HCE_TRACE_METHOD_BODY("try_recv_","closed");
                return { hce::result::closed }; 
            } else [[likely]] { 
                HCE_TRACE_METHOD_BODY("try_recv_","failed");
                return { hce::result::failure }; 
            }
        } else [[likely]] {
            HCE_TRACE_METHOD_BODY("try_recv_","done");
            *((T*)r) = std::move(buf_.front());
            buf_.pop();

            if(parked_send_.size()) [[unlikely]] {
                parked_send_.front()->resume((void*)&buf_);
                parked_send_.pop();
            }

            // return an awaitable which immediately returns true
            return { hce::result::success }; 
        }
    }

    mutable LOCK lk_;
    bool closed_flag_;
    hce::circular_buffer<T> buf_;
    hce::queue<awt<bool>::interface*> parked_send_;
    hce::queue<awt<bool>::interface*> parked_recv_;
};

/// unlimited interface implementation
template <typename T, typename LOCK>
struct unlimited : public interface<T> {
    typedef T value_type;
    static constexpr size_t pool_default_size_limit = 
        hce::queue<T>::pool_default_size_limit;

    unlimited(size_t pool_size_limit = pool_default_size_limit) : 
        closed_flag_(false),
        que_(pool_size_limit),
        parked_send_(pool_size_limit),
        parked_recv_(pool_size_limit)
    { 
        HCE_LOW_CONSTRUCTOR();
    }

    virtual ~unlimited(){ HCE_LOW_DESTRUCTOR(); }

    static inline std::string info_name() { 
        return type::templatize<T,LOCK>("hce::unlimited"); 
    }

    inline std::string name() const { return type::name<unlimited<T,LOCK>>(); }

    inline const std::type_info& type_info() const {
        return typeid(unlimited); 
    }

    inline int size() const {
        HCE_MIN_METHOD_ENTER("size");
        return -1;
    }

    inline int used() const {
        HCE_MIN_METHOD_ENTER("used");

        std::lock_guard<LOCK> lk(lk_);
        return (int)que_.size();
    }

    inline bool closed() const {
        HCE_MIN_METHOD_ENTER("closed");

        std::lock_guard<LOCK> lk(lk_);
        return closed_flag_;
    }

    inline void close() {
        HCE_LOW_METHOD_ENTER("close");

        std::lock_guard<LOCK> lk(lk_);

        if(!closed_flag_) [[unlikely]] {
            closed_flag_ = true;

            while(parked_send_.size()) { 
                HCE_TRACE_METHOD_BODY("close","closing parked send:",parked_send_.front());
                parked_send_.front()->resume(nullptr); 
                parked_send_.pop();
            }

            while(parked_recv_.size()) { 
                HCE_TRACE_METHOD_BODY("close","closing parked recv:",parked_recv_.front());
                parked_recv_.front()->resume(nullptr); 
                parked_recv_.pop();
            }
        }
    }

    inline awt<bool> send(const T& t) {
        HCE_LOW_METHOD_ENTER("send",(void*)&t);
        return send_((void*)&t, false);
    }

    inline awt<bool> send(T&& t) {
        HCE_LOW_METHOD_ENTER("send",(void*)&t);
        return send_((void*)&t, true);
    }

    /**
     Buffered receives will succeed, even if the channel is closed, as long 
     as values are available for retrieval from the internal buffer.
     */
    inline awt<bool> recv(T& t) {
        HCE_LOW_METHOD_ENTER("recv",(void*)&t);
        return recv_((void*)&t);
    }

    inline hce::yield<hce::result> try_send(const T& t) {
        HCE_LOW_METHOD_ENTER("try_send",(void*)&t);
        return try_send_((void*)&t, false);
    }

    inline hce::yield<hce::result> try_send(T&& t) {
        HCE_LOW_METHOD_ENTER("try_send",(void*)&t);
        return try_send_((void*)&t, true);
    }

    /**
     Buffered receives will succeed, even if the channel is closed, as long 
     as values are available for retrieval from the internal buffer.
     */
    inline hce::yield<hce::result> try_recv(T& t) {
        HCE_LOW_METHOD_ENTER("try_recv",(void*)&t);
        return try_recv_((void*)&t);
    }

private:
    inline hce::awt<bool> send_(void* s, bool is_rvalue) {
        HCE_TRACE_METHOD_ENTER("send_",s,is_rvalue);

        struct send_interface : 
            public hce::scheduler::reschedule<
                hce::awaitable::lockable<
                    hce::awt<bool>::interface, 
                    LOCK>>
        {
            send_interface(
                    std::unique_lock<LOCK>& lk, 
                    bool ready, 
                    bool success, 
                    hce::send_pair<T> sp = hce::send_pair<T>()) :
                hce::scheduler::reschedule<
                    hce::awaitable::lockable<
                        hce::awt<bool>::interface, 
                        LOCK>>(
                            *(lk.mutex()),
                            hce::awaitable::await::policy::adopt,
                            hce::awaitable::resume::policy::no_lock),
                ready_(ready),
                success_(success),
                sp_(sp)
            { 
                lk.release();
            }

            inline bool on_ready() { return ready_; }

            inline void on_resume(void* m) {
                if(m) [[likely]] {
                    sp_.send(m);
                    success_ = true;
                }
                else { success_ = false; }
            }

            inline bool get_result() { return success_; }

            bool ready_;
            bool success_;
            hce::send_pair<T> sp_; 
        };

        std::unique_lock<LOCK> lk(lk_);

        if(closed_flag_) [[unlikely]] {
            HCE_TRACE_METHOD_BODY("send_","closed");
            return hce::awt<bool>::make(new send_interface(lk, true, false));
        } else [[likely]] {
            HCE_TRACE_METHOD_BODY("send_","done");
            hce::send_pair<T> sp{s,is_rvalue};
            sp.send((void*)&que_);

            if(parked_recv_.size()) [[unlikely]] {
                parked_recv_.front()->resume((void*)&que_);
                parked_recv_.pop();
            }

            // return an awaitable which immediately returns true
            return hce::awt<bool>::make(new send_interface(lk, true, true));
        }
    }

    /// stackless `co_await`able recv operation
    inline hce::awt<bool> recv_(void* r) {
        HCE_TRACE_METHOD_ENTER("recv_",r);

        struct recv_interface : 
            public hce::scheduler::reschedule<
                hce::awaitable::lockable<
                    hce::awt<bool>::interface,
                    LOCK>>
        {
            recv_interface(
                    std::unique_lock<LOCK>& lk, 
                    bool owned,
                    bool ready, 
                    bool success, 
                    void* r) :
                hce::scheduler::reschedule<
                    hce::awaitable::lockable<
                        hce::awt<bool>::interface,
                        LOCK>>(
                            *(lk.mutex()),
                            hce::awaitable::await::policy::adopt,
                            hce::awaitable::resume::policy::no_lock),
                ready_(ready),
                success_(success),
                r_(r)
            { 
                lk.release();
            }

            inline bool on_ready() { return ready_; }

            inline void on_resume(void* m) {
                if(m) [[likely]] {
                    auto b = (queue<T>*)m;
                    *((T*)r_) = std::move(b->front());
                    b->pop();
                    success_ = true;
                }
                else { success_ = false; }
            }

            inline bool get_result() { return success_; }

            bool ready_;
            bool success_;
            void* r_; // pointer to receiver memory
        };

        std::unique_lock<LOCK> lk(lk_);

        if(que_.empty()) [[unlikely]] {
            if(closed_flag_ ) [[unlikely]] {
                HCE_TRACE_METHOD_BODY("recv_","closed");
                return hce::awt<bool>::make(new recv_interface(lk, true, true, false, nullptr));
            } else [[likely]] {
                HCE_TRACE_METHOD_BODY("recv_","blocked");
                auto ai = new recv_interface(lk, true, false, true, r);
                parked_recv_.push_back(ai);
                return hce::awt<bool>::make(ai);
            }
        } else [[likely]] {
            HCE_TRACE_METHOD_BODY("recv_","done");
            *((T*)r) = std::move(que_.front());
            que_.pop();

            if(parked_send_.size()) [[unlikely]] {
                parked_send_.front()->resume((void*)&que_);
                parked_send_.pop();
            }

            // return an awaitable which immediately returns true
            return hce::awt<bool>::make(new recv_interface(lk, false, true, true, nullptr));
        }
    }
    
    inline hce::yield<hce::result> try_send_(void* s, bool is_rvalue) {
        HCE_TRACE_METHOD_ENTER("try_send_",s,is_rvalue);

        std::unique_lock<LOCK> lk(lk_);

        if(closed_flag_) [[unlikely]] { 
            HCE_TRACE_METHOD_BODY("try_send_","closed");
            return { hce::result::closed }; 
        } else [[likely]] {
            HCE_TRACE_METHOD_BODY("try_send_","done");
            hce::send_pair<T> sp(s,is_rvalue);
            sp.send((void*)&que_);

            if(parked_recv_.size()) [[unlikely]] {
                parked_recv_.front()->resume((void*)&que_);
                parked_recv_.pop();
            }

            // return an awaitable which immediately returns true
            return { hce::result::success }; 
        } 
    }
    
    inline hce::yield<hce::result> try_recv_(void* r) {
        HCE_TRACE_METHOD_ENTER("try_recv_",r);

        std::unique_lock<LOCK> lk(lk_);

        if(que_.empty()) [[unlikely]] {
            if(closed_flag_) [[unlikely]] { 
                HCE_TRACE_METHOD_BODY("try_recv_","closed");
                return { hce::result::closed }; 
            } else [[likely]] { 
                HCE_TRACE_METHOD_BODY("try_recv_","failed");
                return { hce::result::failure }; 
            }
        } else [[likely]] {
            HCE_TRACE_METHOD_BODY("try_recv_","done");
            *((T*)r) = std::move(que_.front());
            que_.pop();

            if(parked_send_.size()) [[unlikely]] {
                parked_send_.front()->resume((void*)&que_);
                parked_send_.pop();
            }

            // return an awaitable which immediately returns true
            return { hce::result::success }; 
        }
    }

    mutable LOCK lk_;
    bool closed_flag_;
    hce::queue<T> que_;
    hce::queue<awt<bool>::interface*> parked_send_;
    hce::queue<awt<bool>::interface*> parked_recv_;
};

}
}

/// allocatable unbuffered channel interface implementation
template <typename T, typename LOCK=hce::spinlock>
using unbuffered = detail::channel::unbuffered<T,LOCK>;

/// allocatable buffered channel interface implementation
template <typename T, typename LOCK=hce::spinlock>
using buffered = detail::channel::buffered<T,LOCK>;

/// allocatable unlimited channel interface implementation
template <typename T, typename LOCK=hce::spinlock>
using unlimited = detail::channel::unlimited<T,LOCK>;

/** 
 @brief communication object which can represent any object which implements channel<T>::interface interface.
*/
template <typename T>
struct channel : public printable {
    /// template type for channel
    typedef T value_type;

    /// default pool_allocator size limit
    static constexpr size_t pool_default_size_limit = 
        hce::queue<T>::pool_default_size_limit;

    /// template type channel interface
    typedef hce::detail::channel::interface<T> interface;

    channel() = default;
    channel(const channel<T>& rhs) = default;
    channel(channel<T>&& rhs) = default;

    virtual ~channel(){ }

    channel<T>& operator=(const channel<T>& rhs) = default;
    channel<T>& operator=(channel<T>&& rhs) = default;

    static inline std::string info_name() { 
        return type::templatize<T>("hce::channel"); 
    }

    inline std::string name() const { return type::name<channel<T>>(); }

    inline std::string content() const { 
        return context_ ? *context_ : std::string(); 
    }

    /// retrieve a copy of the shared context
    inline std::shared_ptr<interface> context() { 
        HCE_TRACE_METHOD_ENTER("context");
        return context_; 
    }

    /// set the shared context
    inline void context(const std::shared_ptr<interface>& i) { 
        HCE_TRACE_METHOD_ENTER("context", i);
        context_ = i;
    }

    /// set the shared context
    inline void context(std::shared_ptr<interface>&& i) { 
        HCE_TRACE_METHOD_ENTER("context", i);
        context_ = std::move(i);
    }

    /**
     @brief construct a channel with an unbuffered implementation

     Specify a LOCK type of `hce::lockfree` to make the implementation lockfree 
     (only safe when all instances of the channel are used from the same system 
     thread). Lockfree channels are a *very* fast way to communicate between two 
     coroutines running on the same scheduler.

     Alternatively, specify a LOCK type of `std::mutex` to potentially improve  
     communication congestion between a large number of threads (not 
     coroutines).

     When in doubt, use the default `hce::spinlock`, it is quite performant in 
     all but extreme edgecases.
     */
    template <typename LOCK=hce::spinlock>
    inline channel<T>& construct() {
        HCE_MIN_METHOD_ENTER("construct");
        context_ = std::shared_ptr<interface>(
            static_cast<interface*>(
                new hce::unbuffered<T,LOCK>()));
        return *this;
    }

    /**
     @brief construct a channel with an implementation 

     Constructed channel is:
     - unbuffered is sz == 0
     - unlimited is sz < 0
     - otherwise buffered with a buffer of sz 

     The pool size limit is the value `pool_allocator`s are constructed with. 
     `pool_allocator`s ensure allocated memory can be reused rather than 
     repeatedly allocating/deallocating. The limit sets the cap of how many 
     allocated values can be reused by the various mechanisms within a channel 
     interface. A higher value can cause more memory, in some cases, to remain 
     allocated, with the benefit that the mechanisms may operate faster. 

     @param sz the size of implementation buffer 
     @param pool_size_limit the size limit passed to pool allocators
     */
    template <typename LOCK=hce::spinlock>
    inline channel<T>& construct(int sz, size_t pool_size_limit = pool_default_size_limit) {
        HCE_MIN_METHOD_ENTER("construct",sz);

        if(sz == 0) {
            context_ = std::shared_ptr<interface>(
                static_cast<interface*>(
                    new hce::unbuffered<T,LOCK>(pool_size_limit)));
        } else if(sz < 0) {
            context_ = std::shared_ptr<interface>(
                static_cast<interface*>(
                    new hce::unlimited<T,LOCK>(pool_size_limit)));
        } else {
            context_ = std::shared_ptr<interface>(
                static_cast<interface*>(
                    new hce::buffered<T,LOCK>(sz, pool_size_limit)));
        }

        return *this;
    }

    /**
     @brief construct a channel inline 
     @return the constructed channel
     */
    template <typename LOCK=hce::spinlock, typename... As>
    inline static channel<T> make(As&&... as) {
        HCE_MIN_FUNCTION_ENTER("make", as...);
        channel<T> ch;
        ch.construct<LOCK>(std::forward<As>(as)...);
        return ch;
    }
       
    /// retrieve the implementation's actual std::type_info
    inline const std::type_info& type_info() const {
        HCE_TRACE_METHOD_ENTER("type_info");
        return context_->type_info(); 
    }

    /// return the maximum buffer size
    inline int size() const {
        HCE_TRACE_METHOD_ENTER("capacity");
        return context_->size(); 
    }

    /// return the number of values in the buffer
    inline int used() const {
        HCE_TRACE_METHOD_ENTER("size");
        return context_->used(); 
    }

    /// return if channel is closed
    inline bool closed() const {
        HCE_TRACE_METHOD_ENTER("closed");
        return context_->closed(); 
    }

    /// close channel
    inline void close() {
        HCE_MIN_METHOD_ENTER("close");
        return context_->close(); 
    }

    /// `co_await`able send operation 
    template <typename T2>
    inline hce::awt<bool> send(T2&& s) {
        HCE_MIN_METHOD_ENTER("send");
        return context_->send(std::forward<T2>(s)); 
    }

    /// `co_await`able recv operation
    inline hce::awt<bool> recv(T& r) {
        HCE_MIN_METHOD_ENTER("recv");
        return context_->recv(r); 
    }

    /// `co_await`able try_send operation
    template <typename T2>
    inline hce::awt<bool> try_send(T2&& s) {
        HCE_MIN_METHOD_ENTER("try_send");
        return context_->try_send(std::forward<T2>(s));
    }

    /// `co_await`able try_recv operation
    inline hce::yield<hce::result> try_recv(T& r) {
        HCE_MIN_METHOD_ENTER("try_recv");
        return context_->try_recv(r);
    }

    /// return whether channel has a shared get pointer
    inline explicit operator bool() const {
        HCE_TRACE_METHOD_ENTER("operator bool");
        return (bool)(context_); 
    }

    /// context address comparison
    inline bool operator==(const channel<T>& rhs) const {
        HCE_TRACE_METHOD_ENTER("operator ==");
        return context_.get() == rhs.context_.get();
    }

    inline bool operator!=(const channel<T>& rhs) const {
        HCE_TRACE_METHOD_ENTER("operator !=");
        return !(*this == rhs);
    }

    inline bool operator<(const channel<T>& rhs) const {
        HCE_TRACE_METHOD_ENTER("operator <");
        return context_.get() < rhs.context_.get();
    }

    inline bool operator<=(const channel<T>& rhs) const {
        HCE_TRACE_METHOD_ENTER("operator <=");
        return context_.get() <= rhs.context_.get();
    }

    inline bool operator>(const channel<T>& rhs) const {
        HCE_TRACE_METHOD_ENTER("operator >");
        return context_.get() > rhs.context_.get();
    }

    inline bool operator>=(const channel<T>& rhs) const {
        HCE_TRACE_METHOD_ENTER("operator >=");
        return context_.get() >= rhs.context_.get();
    }

private:
    std::shared_ptr<interface> context_;
};

}

#endif
