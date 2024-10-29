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
#include "list.hpp"
#include "coroutine.hpp"
#include "scheduler.hpp"

namespace hce {
namespace config {
namespace channel {

/**
 Specify the resource pool allocation limit for allocated channel interfaces.
 */
extern size_t resource_limit();

}
}

/// result of a channel try operation
enum result {
    closed = 0, /// channel is closed
    failure, /// channel operation failed
    success /// channel operation success
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
    >0 == buffer of a specific maximum size
    0 == buffer of no size (direct point to point data transfer)
    <0 == buffer of unlimited size
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

template <typename T>
inline void pointer_send(void* destination, void* source) {
    typedef unqualified<T> U;
    HCE_TRACE_FUNCTION_ENTER("hce::detail::channel::pointer_send", source, destination);
    HCE_TRACE_FUNCTION_BODY("hce::detail::channel::pointer_send", type::name<U>(), " = ", type::name<T>());

    // compiler knows how to select lvalue or rvalue copy since all necessary 
    // type information is available
    *((U*)destination) = std::forward<T>(*((U*)(source))); 
}

template <typename T>
inline void circular_buffer_send(void* destination, void* source) {
    typedef unqualified<T> U;
    typedef hce::circular_buffer<U> BUFFER;
    HCE_TRACE_FUNCTION_ENTER("hce::detail::channel::circular_buffer_send", destination, source);
    HCE_TRACE_FUNCTION_BODY("hce::detail::channel::circular_buffer_send", type::name<U>(), " = ", type::name<T>());
    // compiler knows how to select lvalue or rvalue copy since all necessary 
    // type information is available
    ((BUFFER*)destination)->emplace(std::forward<T>(*((U*)(source)))); 
}

template <typename T>
inline void circular_buffer_recv(void* destination, void* source) {
    typedef hce::circular_buffer<T> BUFFER;
    HCE_TRACE_FUNCTION_ENTER("hce::detail::channel::circular_buffer_recv", destination, source);
    HCE_TRACE_FUNCTION_BODY("hce::detail::channel::circular_buffer_recv", type::name<T>(), " = ", type::name<T&&>());
    // is always a move operation
    *((T*)destination) = std::move(((BUFFER*)source)->front());
    ((BUFFER*)source)->pop();
}

template <typename T, typename LIST>
inline void list_send(void* destination, void* source) {
    typedef unqualified<T> U;
    HCE_TRACE_FUNCTION_ENTER("hce::detail::channel::list_send", destination, source);
    HCE_TRACE_FUNCTION_BODY("hce::detail::channel::list_send", type::name<U>(), " = ", type::name<T>());
    // compiler knows how to select lvalue or rvalue copy since all necessary 
    // type information is available
    ((LIST*)destination)->emplace_back(std::forward<T>(*((U*)(source)))); 
}

template <typename T, typename LIST>
inline void list_recv(void* destination, void* source) {
    HCE_TRACE_FUNCTION_ENTER("hce::detail::channel::list_recv", destination, source);
    HCE_TRACE_FUNCTION_BODY("hce::detail::channel::list_recv", type::name<T>(), " = ", type::name<T&&>());
    auto& que = *((LIST*)source);
    // is always a move operation
    *((T*)destination) = std::move(que.front());
    que.pop();
}

/*
 The actual copy/move of data T from source to destination is abstracted by this 
 object because copy/move semantics can be preserved without having to compile 
 both variants (which is invalid for arbitrary types T).

 Rather than accepting void*, the constructor and `send()` accept any source 
 or destination pointer type and will cast implicitly for the user.
 */
struct transfer {
    typedef void (*op_f)(void*,void*);

    template <typename T>
    transfer(op_f op, T* source) : op_(op), source_((void*)source) { }


    template <typename T>
    inline void send(T* destination) {
        op_((void*)destination, source_);
    }

private:
    void (*op_)(void*,void*);
    void* source_;
};

template <typename LOCK>
struct base_send_interface : 
    public hce::scheduler::reschedule<
        hce::awaitable::lockable<
            awt_interface<bool>,
            LOCK>>
{
    base_send_interface(LOCK& lk, detail::channel::transfer t) : 
        hce::scheduler::reschedule<
            hce::awaitable::lockable<
                awt_interface<bool>,
                LOCK>>(
                    lk,
                    hce::awaitable::await::policy::defer,
                    hce::awaitable::resume::policy::no_lock),
        tx(t)
    { }

    inline void on_resume(void* m) {
        HCE_MIN_METHOD_ENTER("on_resume",m);

        if(m) [[likely]] {
            // m is destination
            tx.send(m);
            success = true;
        }
    }

    inline bool get_result() { 
        HCE_MIN_METHOD_BODY("get_result",success);
        return success; 
    }

    bool success = false;
    detail::channel::transfer tx;
};

template <typename LOCK>
struct base_recv_interface : 
    public hce::scheduler::reschedule<
        hce::awaitable::lockable<
            awt_interface<bool>,
            LOCK>>
{
    base_recv_interface(LOCK& lk, void* d) : 
        hce::scheduler::reschedule<
            hce::awaitable::lockable<
                awt_interface<bool>,
                LOCK>>(
                    lk,
                    hce::awaitable::await::policy::defer,
                    hce::awaitable::resume::policy::no_lock),
        destination(d)
    { }

    inline void on_resume(void* m) {
        HCE_TRACE_METHOD_BODY("on_resume",m);

        if(m) [[likely]] {
            // m is transfer struct
            ((transfer*)m)->send(destination);
            success = true;
        }
    }

    inline bool get_result() { 
        HCE_MIN_METHOD_BODY("get_result",success);
        return success; 
    }

    bool success = false;
    void* destination = nullptr;
};

/// unbuffered interface implementation
template <typename T, typename LOCK, typename ALLOCATOR>
struct unbuffered : public interface<T> {
    typedef T value_type;

    unbuffered() {
        HCE_LOW_CONSTRUCTOR(); 
    }

    unbuffered(const ALLOCATOR& allocator) : 
        parked_send_(allocator),
        parked_recv_(allocator)
    { 
        HCE_LOW_CONSTRUCTOR(); 
    }
    
    virtual ~unbuffered(){ HCE_LOW_DESTRUCTOR(); }

    static inline hce::string info_name() { 
        return type::templatize<T,LOCK,ALLOCATOR>("hce::unbuffered"); 
    }

    inline hce::string name() const { 
        return unbuffered<T,LOCK,ALLOCATOR>::info_name(); 
    }

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

        std::lock_guard<LOCK> lk(lk_);
        return closed_flag_;
    }

    inline void close() {
        HCE_LOW_METHOD_ENTER("close");

        std::lock_guard<LOCK> lk(lk_);
        if(!closed_flag_) [[likely]] {
            closed_flag_ = true;

            while(parked_send_.size()) { 
                parked_send_.front()->resume(nullptr); 
                parked_send_.pop();
            }

            while(parked_recv_.size()) { 
                parked_recv_.front()->resume(nullptr); 
                parked_recv_.pop();
            }
        }
    }

    inline awt<bool> send(const T& s) {
        HCE_LOW_METHOD_ENTER("send",(void*)&s);

        return hce::awt<bool>(new send_interface(
            *this, transfer(pointer_send<const T&>,&s)));
    }

    inline awt<bool> send(T&& s) {
        HCE_LOW_METHOD_ENTER("send",(void*)&s);

        return hce::awt<bool>(new send_interface(
            *this, transfer(pointer_send<T&&>,&s)));
    }

    inline awt<bool> recv(T& r) {
        HCE_LOW_METHOD_ENTER("recv",(void*)&r);

        return hce::awt<bool>(new recv_interface(*this, (void*)&r));
    }

    inline hce::yield<hce::result> try_send(const T& s) {
        HCE_LOW_METHOD_ENTER("try_send",(void*)&s);
        return try_send_(s);
    }

    inline hce::yield<hce::result> try_send(T&& s) {
        HCE_LOW_METHOD_ENTER("try_send",(void*)&s);
        return try_send_(std::move(s));
    }

    inline hce::yield<hce::result> try_recv(T& r) {
        HCE_LOW_METHOD_ENTER("try_recv",(void*)&r);

        std::lock_guard<LOCK> lk(lk_);

        if(closed_flag_) [[unlikely]] { 
            return { hce::result::closed }; 
        } else if(parked_send_.size()) [[likely]] {
            parked_send_.front()->resume((void*)&r);

            // return an awaitable which immediately returns true
            return { hce::result::success }; 
        } else [[unlikely]] { return { hce::result::failure }; }
    }

private:
    typedef unbuffered<T,LOCK,ALLOCATOR> PARENT;

    struct send_interface : 
        public base_send_interface<LOCK> 
    {
        send_interface(PARENT& p, transfer tx) :
            base_send_interface<LOCK>(p.lk_, tx),
            parent_(p)
        { }

        static inline hce::string info_name() {
            return detail::channel::unbuffered<T,LOCK,ALLOCATOR>::info_name() + 
                   "::send_interface";
        };

        inline hce::string name() const { return send_interface::info_name(); }

        inline bool on_ready() {
            if(parent_.closed_flag_) [[unlikely]] {
                HCE_TRACE_METHOD_BODY("send","closed");
                return true;
            } else if(parent_.parked_recv_.size()) [[likely]] {
                HCE_TRACE_METHOD_BODY("send","done");
                parent_.parked_recv_.front()->resume((void*)&(this->tx));
                parent_.parked_recv_.pop();
                this->success = true;
                return true;
            } else [[unlikely]] {
                HCE_TRACE_METHOD_BODY("send","blocked");
                parent_.parked_send_.push_back(this);
                return false;
            }
        }

    private:
        PARENT& parent_;
    };

    struct recv_interface : public base_recv_interface<LOCK> {
        recv_interface(PARENT& p, void* destination) :
            base_recv_interface<LOCK>(p.lk_, destination),
            parent_(p)
        { }

        static inline hce::string info_name() {
            return detail::channel::unbuffered<T,LOCK,ALLOCATOR>::info_name() + 
                   "::recv_interface";
        };

        inline hce::string name() const { return recv_interface::info_name(); }

        inline bool on_ready() {
            if(parent_.closed_flag_) [[unlikely]] { 
                HCE_TRACE_METHOD_BODY("recv","closed");
                return true;
            } else if(parent_.parked_send_.size()) [[likely]] {
                HCE_TRACE_METHOD_BODY("recv","resume");
                parent_.parked_send_.front()->resume(this->destination);
                parent_.parked_send_.pop();
                this->success = true;
                return true;
            } else [[unlikely]] {
                HCE_TRACE_METHOD_BODY("recv","block for transfer");
                parent_.parked_recv_.push_back(this);
                return false;
            }
        }

    private:
        PARENT& parent_;
    };
    
    template <typename U>
    inline hce::yield<hce::result> try_send_(U&& s) {
        std::lock_guard<LOCK> lk(lk_);

        if(closed_flag_) [[unlikely]] { 
            HCE_TRACE_METHOD_BODY("try_send","closed");
            return { hce::result::closed }; 
        } else if(parked_recv_.size()) [[likely]] {
            HCE_TRACE_METHOD_BODY("try_send","done");
            transfer tx(pointer_send<U>,&s);
            parked_recv_.front()->resume((void*)(&tx));
            parked_recv_.pop();

            return { hce::result::success }; 
        } else [[unlikely]] { return { hce::result::failure }; }
    }

    mutable LOCK lk_;
    bool closed_flag_ = false;
    hce::list<awt<bool>::interface*,ALLOCATOR> parked_send_;
    hce::list<awt<bool>::interface*,ALLOCATOR> parked_recv_;
};

/// buffered interface implementation
template <typename T, typename LOCK, typename ALLOCATOR>
struct buffered : public interface<T> {
    typedef T value_type;

    buffered(int sz) : 
        buf_(sz ? (size_t)sz : (size_t)1),
        parked_send_(),
        parked_recv_()
    { 
        HCE_LOW_CONSTRUCTOR();
    }

    buffered(int sz, const ALLOCATOR& allocator) : 
        buf_(sz ? (size_t)sz : (size_t)1),
        parked_send_(allocator),
        parked_recv_(allocator)
    { 
        HCE_LOW_CONSTRUCTOR();
    }

    virtual ~buffered(){ HCE_LOW_DESTRUCTOR(); }

    static inline hce::string info_name() { 
        return type::templatize<T,LOCK,ALLOCATOR>("hce::buffered"); 
    }

    inline hce::string name() const { 
        return buffered<T,LOCK,ALLOCATOR>::info_name(); 
    }

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
                parked_send_.front()->resume(nullptr); 
                parked_send_.pop();
            }

            while(parked_recv_.size()) { 
                parked_recv_.front()->resume(nullptr); 
                parked_recv_.pop();
            }
        }
    }

    inline awt<bool> send(const T& s) {
        HCE_LOW_METHOD_ENTER("send",(void*)&s);

        return hce::awt<bool>(new send_interface(
            *this, transfer(circular_buffer_send<const T&>,(void*)&s)));
    }

    inline awt<bool> send(T&& s) {
        HCE_LOW_METHOD_ENTER("send",(void*)&s);

        return hce::awt<bool>(new send_interface(
            *this, transfer(circular_buffer_send<T&&>,(void*)&s)));
    }

    inline awt<bool> recv(T& r) {
        HCE_LOW_METHOD_ENTER("recv",(void*)&r);

        return hce::awt<bool>(new recv_interface(*this, (void*)&r));
    }

    inline hce::yield<hce::result> try_send(const T& t) {
        HCE_LOW_METHOD_ENTER("try_send",(void*)&t);
        return try_send_(t);
    }

    inline hce::yield<hce::result> try_send(T&& t) {
        HCE_LOW_METHOD_ENTER("try_send",(void*)&t);
        return try_send_(std::move(t));
    }

    /**
     Buffered receives will succeed, even if the channel is closed, as long 
     as values are available for retrieval from the internal buffer.
     */
    inline hce::yield<hce::result> try_recv(T& r) {
        HCE_LOW_METHOD_ENTER("try_recv",(void*)&r);

        std::lock_guard<LOCK> lk(lk_);

        if(buf_.empty()) [[unlikely]] {
            if(closed_flag_) [[unlikely]] { 
                HCE_TRACE_METHOD_BODY("try_recv","closed");
                return { hce::result::closed }; 
            } else [[likely]] { 
                HCE_TRACE_METHOD_BODY("try_recv","failed");
                return { hce::result::failure }; 
            }
        } else [[likely]] {
            HCE_TRACE_METHOD_BODY("try_recv","done");
            r = std::move(buf_.front());
            buf_.pop();

            if(parked_send_.size()) [[unlikely]] {
                parked_send_.front()->resume((void*)&buf_);
                parked_send_.pop();
            }

            // return an awaitable which immediately returns true
            return { hce::result::success }; 
        }
    }

private:
    typedef buffered<T,LOCK,ALLOCATOR> PARENT;

    struct send_interface : public base_send_interface<LOCK> {
        send_interface(PARENT& p, transfer tx) :
            base_send_interface<LOCK>(p.lk_, tx),
            parent_(p)
        { }

        static inline hce::string info_name() {
            return detail::channel::buffered<T,LOCK,ALLOCATOR>::info_name() + 
                   "::send_interface";
        };

        inline hce::string name() const { return send_interface::info_name(); }

        inline bool on_ready() {
            if(parent_.closed_flag_) [[unlikely]] {
                HCE_TRACE_METHOD_BODY("send","closed");
                return false;
            } else if(parent_.buf_.full()) [[unlikely]] {
                HCE_TRACE_METHOD_BODY("send","blocked");
                parent_.parked_send_.push_back(this);
                return false;
            } else [[likely]] {
                HCE_TRACE_METHOD_BODY("send","done");
                this->tx.send(&(parent_.buf_));
                this->success = true;

                if(parent_.parked_recv_.size()) [[unlikely]] {
                    transfer tx(circular_buffer_recv<T>,&(parent_.buf_));
                    parent_.parked_recv_.front()->resume((void*)&(tx));
                    parent_.parked_recv_.pop();
                }

                return true;
            }
        }

    private:
        PARENT& parent_;
    };

    struct recv_interface : public base_recv_interface<LOCK> {
        recv_interface(PARENT& p, void* destination) :
            base_recv_interface<LOCK>(p.lk_, destination),
            parent_(p)
        { }

        static inline hce::string info_name() {
            return detail::channel::buffered<T,LOCK,ALLOCATOR>::info_name() + 
                   "::recv_interface";
        };

        inline hce::string name() const { return recv_interface::info_name(); }

        inline bool on_ready() {
            if(parent_.buf_.empty()) [[unlikely]] {
                if(parent_.closed_flag_ ) [[unlikely]] {
                    HCE_TRACE_METHOD_BODY("recv","closed");
                    return true;
                } else [[likely]] {
                    HCE_TRACE_METHOD_BODY("recv","blocked");
                    parent_.parked_recv_.push_back(this);
                    return false;
                }
            } else [[likely]] {
                HCE_TRACE_METHOD_BODY("recv","done");
                transfer tx(circular_buffer_recv<T>,&(parent_.buf_));
                tx.send(this->destination);
                this->success = true;

                if(parent_.parked_send_.size()) [[unlikely]] {
                    parent_.parked_send_.front()->resume((void*)&(parent_.buf_));
                    parent_.parked_send_.pop();
                }

                return true;
            }
        }

    private:
        PARENT& parent_;
    };

    template <typename U>
    inline hce::yield<hce::result> try_send_(U&& s) {
        std::lock_guard<LOCK> lk(lk_);

        if(closed_flag_) [[unlikely]] { 
            HCE_TRACE_METHOD_BODY("try_send","closed");
            return { hce::result::closed }; 
        } else if(!buf_.full()) [[likely]] {
            HCE_TRACE_METHOD_BODY("try_send","done");
            buf_.push(std::forward<U>(s));

            if(parked_recv_.size()) [[unlikely]] {
                parked_recv_.front()->resume((void*)&buf_);
                parked_recv_.pop();
            }

            return { hce::result::success }; 
        } else [[unlikely]] { 
            HCE_TRACE_METHOD_BODY("try_send","failed");
            return { hce::result::failure }; 
        }
    }

    mutable LOCK lk_;
    bool closed_flag_ = false;
    hce::circular_buffer<T> buf_;
    hce::list<awt<bool>::interface*,ALLOCATOR> parked_send_;
    hce::list<awt<bool>::interface*,ALLOCATOR> parked_recv_;
};

/// unlimited interface implementation
template <typename T, typename LOCK, typename ALLOCATOR>
struct unlimited : public interface<T> {
    typedef T value_type;

    unlimited() { 
        HCE_LOW_CONSTRUCTOR();
    }

    unlimited(const ALLOCATOR& allocator) : 
        queue_(allocator),
        parked_recv_(allocator)
    { 
        HCE_LOW_CONSTRUCTOR();
    }

    virtual ~unlimited(){ HCE_LOW_DESTRUCTOR(); }

    static inline hce::string info_name() { 
        return type::templatize<T,LOCK,ALLOCATOR>("hce::unlimited"); 
    }

    inline hce::string name() const { 
        return unlimited<T,LOCK,ALLOCATOR>::info_name(); 
    }

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
        return (int)queue_.size();
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

            while(parked_recv_.size()) { 
                HCE_TRACE_METHOD_BODY("close","closing parked recv:",parked_recv_.front());
                parked_recv_.front()->resume(nullptr); 
                parked_recv_.pop();
            }
        }
    }

    inline awt<bool> send(const T& s) {
        HCE_LOW_METHOD_ENTER("send",(void*)&s);
        return awt<bool>(new send_interface(
            *this, 
            transfer(list_send<const T&,hce::list<T,ALLOCATOR>>,&s)));
    }

    inline awt<bool> send(T&& s) {
        HCE_LOW_METHOD_ENTER("send",(void*)&s);
        return awt<bool>(new send_interface(
            *this, 
            transfer(list_send<T&&,hce::list<T,ALLOCATOR>>,&s)));
    }

    /**
     Buffered receives will succeed, even if the channel is closed, as long 
     as values are available for retrieval from the internal buffer.
     */
    inline awt<bool> recv(T& r) {
        HCE_LOW_METHOD_ENTER("recv",(void*)&r);
        return awt<bool>(new recv_interface(*this, (void*)&r));
    }

    inline hce::yield<hce::result> try_send(const T& t) {
        HCE_LOW_METHOD_ENTER("try_send",(void*)&t);
        return try_send_(t);
    }

    inline hce::yield<hce::result> try_send(T&& t) {
        HCE_LOW_METHOD_ENTER("try_send",(void*)&t);
        return try_send_(std::move(t));
    }

    /**
     Buffered receives will succeed, even if the channel is closed, as long 
     as values are available for retrieval from the internal buffer.
     */
    inline hce::yield<hce::result> try_recv(T& r) {
        HCE_LOW_METHOD_ENTER("try_recv",(void*)&r);

        std::lock_guard<LOCK> lk(lk_);

        if(queue_.empty()) [[unlikely]] {
            if(closed_flag_) [[unlikely]] { 
                HCE_TRACE_METHOD_BODY("try_recv","closed");
                return { hce::result::closed }; 
            } else [[likely]] { 
                HCE_TRACE_METHOD_BODY("try_recv","failed");
                return { hce::result::failure }; 
            }
        } else [[likely]] {
            HCE_TRACE_METHOD_BODY("try_recv","done");
            r = std::move(queue_.front());
            queue_.pop();

            // return an awaitable which immediately returns true
            return { hce::result::success }; 
        }
    }

private:
    typedef unlimited<T,LOCK,ALLOCATOR> PARENT;

    struct send_interface : public base_send_interface<LOCK> {
        send_interface(PARENT& p, transfer tx) :
            base_send_interface<LOCK>(p.lk_, tx),
            parent_(p)
        { }

        static inline hce::string info_name() {
            return detail::channel::unlimited<T,LOCK,ALLOCATOR>::info_name() + 
                   "::send_interface";
        };

        inline hce::string name() const { return send_interface::info_name(); }

        inline bool on_ready() { 
            if(parent_.closed_flag_) [[unlikely]] {
                HCE_TRACE_METHOD_BODY("send","closed");
                return true;
            } else [[likely]] {
                HCE_TRACE_METHOD_BODY("send","done");
                this->tx.send(&(parent_.queue_));
                this->success = true;

                if(parent_.parked_recv_.size()) [[unlikely]] {
                    transfer tx(&list_recv<T,hce::list<T,ALLOCATOR>>,&(parent_.queue_));
                    parent_.parked_recv_.front()->resume((void*)&tx);
                    parent_.parked_recv_.pop();
                }

                return true;
            }
        }

    private:
        PARENT& parent_;
    };

    struct recv_interface : public base_recv_interface<LOCK> {
        recv_interface(PARENT& p, void* destination) :
            base_recv_interface<LOCK>(p.lk_, destination),
            parent_(p)
        { }

        static inline hce::string info_name() {
            return detail::channel::unlimited<T,LOCK,ALLOCATOR>::info_name() + 
                   "::recv_interface";
        };

        inline hce::string name() const { return recv_interface::info_name(); }

        inline bool on_ready() {
            if(parent_.queue_.empty()) [[unlikely]] {
                if(parent_.closed_flag_ ) [[unlikely]] {
                    HCE_TRACE_METHOD_BODY("recv","closed");
                    return true;
                } else [[likely]] {
                    HCE_TRACE_METHOD_BODY("recv","blocked");
                    parent_.parked_recv_.push_back(this);
                    return false;
                }
            } else [[likely]] {
                HCE_TRACE_METHOD_BODY("recv_","done");
                transfer tx(&list_recv<T,hce::list<T,ALLOCATOR>>,&(parent_.queue_));
                tx.send(this->destination);
                this->success = true;
                return true;
            }
        }

    private:
        PARENT& parent_;
    };
    
    template <typename U>
    inline hce::yield<hce::result> try_send_(U&& s) {
        std::lock_guard<LOCK> lk(lk_);

        if(closed_flag_) [[unlikely]] { 
            HCE_TRACE_METHOD_BODY("try_send","closed");
            return { hce::result::closed }; 
        } else [[likely]] {
            HCE_TRACE_METHOD_BODY("try_send","done");
            queue_.push_back(std::forward<U>(s));

            if(parked_recv_.size()) [[unlikely]] {
                transfer tx(&list_recv<T,hce::list<T,ALLOCATOR>>, &queue_);
                parked_recv_.front()->resume((void*)&tx);
                parked_recv_.pop();
            }

            // return an awaitable which immediately returns true
            return { hce::result::success }; 
        } 
    }

    mutable LOCK lk_;
    bool closed_flag_ = false;
    hce::list<T,ALLOCATOR> queue_;

    // send() never blocks, so only has parked recv queue
    hce::list<awt<bool>::interface*,ALLOCATOR> parked_recv_;
};

}
}

/// allocatable unbuffered channel interface implementation
template <typename T, typename LOCK=hce::spinlock, typename ALLOCATOR=hce::pool_allocator<T>>
using unbuffered = detail::channel::unbuffered<T,LOCK,ALLOCATOR>;

/// allocatable buffered channel interface implementation
template <typename T, typename LOCK=hce::spinlock, typename ALLOCATOR=hce::pool_allocator<T>>
using buffered = detail::channel::buffered<T,LOCK,ALLOCATOR>;

/// allocatable unlimited channel interface implementation
template <typename T, typename LOCK=hce::spinlock, typename ALLOCATOR=hce::pool_allocator<T>>
using unlimited = detail::channel::unlimited<T,LOCK,ALLOCATOR>;

/** 
 @brief communication object which can represent any object which implements channel<T>::interface interface.
*/
template <typename T>
struct channel : public printable {
    /// template type for channel
    typedef T value_type;

    /// template type channel interface
    typedef hce::detail::channel::interface<T> interface;

    channel() = default;
    channel(const channel<T>& rhs) = default;
    channel(channel<T>&& rhs) = default;

    virtual ~channel(){ }

    channel<T>& operator=(const channel<T>& rhs) = default;
    channel<T>& operator=(channel<T>&& rhs) = default;

    static inline hce::string info_name() { 
        return type::templatize<T>("hce::channel"); 
    }

    inline hce::string name() const { return channel<T>::info_name(); }

    inline hce::string content() const { 
        hce::stringstream ss;
        ss << context_.get();
        return ss.str();
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
     @brief construct a channel with with one of several possible implementations 

     Constructed channel is:
     - sz == 0: unbuffered implementation
     - sz < 0: unlimited implementation
     - otherwise buffered implementation with a buffer of sz 

     Specify a LOCK type of `hce::lockfree` to make the implementation lockfree 
     (only safe when all instances of the channel are used from the same system 
     thread). LOCKfree channels are a *very* fast way to communicate between two 
     coroutines running on the same scheduler.

     Alternatively, specify a LOCK type of `std::mutex` to potentially improve  
     communication congestion between a large number of threads (not 
     coroutines).

     When in doubt, use the default `hce::spinlock`, it is quite performant in 
     all but extreme edgecases.

     @param sz the size of implementation buffer 
     @param as optional arguments for implementation constructor
     */
    template <typename LOCK=hce::spinlock, 
              typename ALLOCATOR=hce::pool_allocator<T>,
              typename... As>
    inline channel<T>& construct(int sz=0, As&&... as) {
        HCE_MIN_METHOD_ENTER("construct",sz,as...);

        if(sz == 0) {
            context_ = std::shared_ptr<interface>(
                static_cast<interface*>(
                    new hce::unbuffered<T,LOCK,ALLOCATOR>(std::forward<As>(as)...)));
        } else if(sz < 0) {
            context_ = std::shared_ptr<interface>(
                static_cast<interface*>(
                    new hce::unlimited<T,LOCK,ALLOCATOR>(std::forward<As>(as)...)));
        } else {
            context_ = std::shared_ptr<interface>(
                static_cast<interface*>(
                    new hce::buffered<T,LOCK,ALLOCATOR>(sz, std::forward<As>(as)...)));
        }

        return *this;
    }

    /**
     @brief construct a channel inline 
     @return the constructed channel
     */
    template <typename LOCK=hce::spinlock, 
              typename ALLOCATOR=hce::pool_allocator<T>, 
              typename... As>
    inline static channel<T> make(As&&... as) {
        HCE_MIN_FUNCTION_ENTER(
            channel<T>::info_name() +
            type::templatize<LOCK,ALLOCATOR>("::make"), as...);
        channel<T> ch;
        ch.construct<LOCK,ALLOCATOR>(std::forward<As>(as)...);
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
