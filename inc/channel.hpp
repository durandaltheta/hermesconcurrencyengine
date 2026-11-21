#pragma once
//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
// c++
#include <typeinfo>
#include <mutex>
#include <functional>
#include <utility>
#include <vector>
#include <atomic>

// local
#include "utility.hpp"
#include "logging.hpp"
#include "memory.hpp"
#include "atomic.hpp"
#include "alloc.hpp"
#include "circular_buffer.hpp"
#include "list.hpp"
#include "coroutine.hpp"
#include "scheduler.hpp"

namespace hce {

struct multiplex;

namespace channel {
namespace detail {

// Used to specially control blocking behavior of the channel interface. 
struct guard {
    // all values here are set by a parent object
    struct parent_t {
        void* ctx = nullptr; // a context pointer (will already be set)

        /*
         Called by the interface *every* time a send occurs. Returns true if the 
         send can proceed, and false if the channel must block.

         Accepts the ctx context pointer.
         */
        bool (*try_claim)(void*) = nullptr;
    };


    // all values must be assigned by the interface during set_guard()
    struct child_t {
        void* ctx = nullptr; // pointer to the implementation

        /*
         Called by the parent when the channel must become unblocked. Passes in 
         self as the argument.
         */
        void (*unblock)(void*) = nullptr;
    };

    const parent_t parent;
    std::vector<child_t> children;
};

} // detail

/// result of a channel try operation
enum result {
    closed = 0, /// channel is closed
    failure, /// channel operation failed
    success /// channel operation success
};

/**
 @brief interface for coroutine-safe channel implementations
 */
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
    virtual hce::yield<channel::result> try_send(const T& t) = 0;

    /// attempt to send a rvalue copy
    virtual hce::yield<channel::result> try_send(T&& t) = 0;

    /// attempt receive a value
    virtual hce::yield<channel::result> try_recv(T& t) = 0;

protected:
    /// register a guard object
    virtual void set_guard(detail::guard*) = 0;

    friend struct multiplex;
};

namespace detail {

template <typename T>
inline void pointer_send(void* destination, void* source) {
    typedef unqualified<T> U;
    HCE_TRACE_FUNCTION_ENTER("hce::channel::detail::pointer_send", source, destination);
    HCE_TRACE_FUNCTION_BODY("hce::channel::detail::pointer_send", type::name<U>(), " = ", type::name<T>());

    // compiler knows how to select lvalue or rvalue copy since all necessary 
    // type information is available
    *((U*)destination) = std::forward<T>(*((U*)(source))); 
}

template <typename T>
inline void circular_buffer_send(void* destination, void* source) {
    typedef unqualified<T> U;
    typedef hce::circular_buffer<U> BUFFER;
    HCE_TRACE_FUNCTION_ENTER("hce::channel::detail::circular_buffer_send", destination, source);
    HCE_TRACE_FUNCTION_BODY("hce::channel::detail::circular_buffer_send", type::name<U>(), " = ", type::name<T>());
    // compiler knows how to select lvalue or rvalue copy since all necessary 
    // type information is available
    ((BUFFER*)destination)->emplace(std::forward<T>(*((U*)(source)))); 
}

template <typename T>
inline void circular_buffer_recv(void* destination, void* source) {
    typedef hce::circular_buffer<T> BUFFER;
    HCE_TRACE_FUNCTION_ENTER("hce::channel::detail::circular_buffer_recv", destination, source);
    HCE_TRACE_FUNCTION_BODY("hce::channel::detail::circular_buffer_recv", type::name<T>(), " = ", type::name<T&&>());
    // is always a move operation
    *((T*)destination) = std::move(((BUFFER*)source)->front());
    ((BUFFER*)source)->pop();
}

template <typename T, typename LIST>
inline void list_send(void* destination, void* source) {
    typedef unqualified<T> U;
    HCE_TRACE_FUNCTION_ENTER("hce::channel::detail::list_send", destination, source);
    HCE_TRACE_FUNCTION_BODY("hce::channel::detail::list_send", type::name<U>(), " = ", type::name<T>());
    // compiler knows how to select lvalue or rvalue copy since all necessary 
    // type information is available
    ((LIST*)destination)->emplace_back(std::forward<T>(*((U*)(source)))); 
}

template <typename T, typename LIST>
inline void list_recv(void* destination, void* source) {
    HCE_TRACE_FUNCTION_ENTER("hce::channel::detail::list_recv", destination, source);
    HCE_TRACE_FUNCTION_BODY("hce::channel::detail::list_recv", type::name<T>(), " = ", type::name<T&&>());
    auto& que = *((LIST*)source);
    // is always a move operation
    *((T*)destination) = std::move(que.front());
    que.pop();
}

/*
 The actual copy/move of data T from source to destination is abstracted by this 
 object because copy/move semantics can be preserved without having to compile 
 both variants (which is potentially invalid for arbitrary types T).

 This object requires a function pointer to do the data transfer. This function 
 pointer can be for either a deep or shallow copy, as required by the usecase 
 that constructs it.

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
            LOCK,
            awt_interface<bool>>>
{
    base_send_interface(LOCK& lk, transfer t) : 
        hce::scheduler::reschedule<
            hce::awaitable::lockable<
                LOCK,
                awt_interface<bool>>>(
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
    transfer tx;
};

template <typename LOCK>
struct base_recv_interface : 
    public hce::scheduler::reschedule<
        hce::awaitable::lockable<
            LOCK,
            awt_interface<bool>>>
{
    base_recv_interface(LOCK& lk, void* d) : 
        hce::scheduler::reschedule<
            hce::awaitable::lockable<
                LOCK,
                awt_interface<bool>>>(
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

}

/// unbuffered interface implementation
template <typename T, typename LOCK=hce::spinlock, typename ALLOCATOR=hce::pool_allocator<T>>
struct unbuffered : public interface<T> {
    typedef unbuffered<T,LOCK,ALLOCATOR> self_type;_
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

    unbuffered(const unbuffered<T,LOCK,ALLOCATOR>&) = delete;
    unbuffered(unbuffered<T,LOCK,ALLOCATOR>&&) = delete;
    
    inline virtual ~unbuffered(){ HCE_LOW_DESTRUCTOR(); }

    unbuffered<T,LOCK,ALLOCATOR>& operator=(const unbuffered<T,LOCK,ALLOCATOR>&) = delete;
    unbuffered<T,LOCK,ALLOCATOR>& operator=(unbuffered<T,LOCK,ALLOCATOR>&&) = delete;

    static inline std::string info_name() { 
        return type::templatize<T,LOCK,ALLOCATOR>("hce::channel::unbuffered"); 
    }

    inline std::string name() const { 
        return unbuffered<T,LOCK,ALLOCATOR>::info_name(); 
    }

    inline const std::type_info& type_info() const {
        HCE_TRACE_METHOD_ENTER("type_info");
        return typeid(unbuffered<T,LOCK,ALLOCATOR>); 
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
            *this, detail::transfer(detail::pointer_send<const T&>,&s)));
    }

    inline awt<bool> send(T&& s) {
        HCE_LOW_METHOD_ENTER("send",(void*)&s);

        return hce::awt<bool>(new send_interface(
            *this, detail::transfer(detail::pointer_send<T&&>,&s)));
    }

    inline awt<bool> recv(T& r) {
        HCE_LOW_METHOD_ENTER("recv",(void*)&r);

        return hce::awt<bool>(new recv_interface(*this, (void*)&r));
    }

    inline hce::yield<result> try_send(const T& s) {
        HCE_LOW_METHOD_ENTER("try_send",(void*)&s);
        return try_send_(s);
    }

    inline hce::yield<result> try_send(T&& s) {
        HCE_LOW_METHOD_ENTER("try_send",(void*)&s);
        return try_send_(std::move(s));
    }

    inline hce::yield<result> try_recv(T& r) {
        HCE_LOW_METHOD_ENTER("try_recv",(void*)&r);

        std::lock_guard<LOCK> lk(lk_);

        if(closed_flag_) [[unlikely]] { 
            return { result::closed }; 
        } else if(!parked_send_.size() ||
                  (parent_.guard_ && 
                   !parent_.guard_.try_claim(parent_.guard_.ctx))) [[unlikely]] {
            return { result::failure }; 
        } else if() [[likely]] {
            parked_send_.front()->resume((void*)&r);

            // return an awaitable which immediately returns true
            return { result::success }; 
        }
    }

protected:
    inline void set_guard(detail::guard* g) { 
        HCE_LOW_METHOD_ENTER("set_guard");
        std::lock_guard<LOCK> lk(lk_);
        
        guard_ = g; 

        if(guard_) {
            guard_->children.emplace_back(
                this,
                [](void* v) { ((self_type*)v)->unblock_(); }
            );
        }
    }

private:
    typedef unbuffered<T,LOCK,ALLOCATOR> PARENT;

    struct send_interface : 
        public detail::base_send_interface<LOCK> 
    {
        send_interface(PARENT& p, detail::transfer tx) :
            detail::base_send_interface<LOCK>(p.lk_, tx),
            parent_(p)
        { }

        static inline std::string info_name() {
            return unbuffered<T,LOCK,ALLOCATOR>::info_name() + 
                   "::send_interface";
        };

        inline std::string name() const { return send_interface::info_name(); }

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

    struct recv_interface : public detail::base_recv_interface<LOCK> {
        recv_interface(PARENT& p, void* destination) :
            detail::base_recv_interface<LOCK>(p.lk_, destination),
            parent_(p)
        { }

        static inline std::string info_name() {
            return unbuffered<T,LOCK,ALLOCATOR>::info_name() + 
                   "::recv_interface";
        };

        inline std::string name() const { return recv_interface::info_name(); }

        inline bool on_ready() {
            if(parent_.closed_flag_) [[unlikely]] { 
                HCE_TRACE_METHOD_BODY("recv","closed");
                return true;
            } else if(!parent_.parked_send_.size() ||
                      (parent_.guard_ && 
                       !parent_.guard_.try_claim(parent_.guard_.ctx))) [[unlikely]] {
                HCE_TRACE_METHOD_BODY("recv","block for transfer");
                parent_.parked_recv_.push_back(this);
                return false;
            } else [[likely]] {
                HCE_TRACE_METHOD_BODY("recv","resume");
                parent_.parked_send_.front()->resume(this->destination);
                parent_.parked_send_.pop();
                this->success = true;
                return true;
            } 
        }

    private:
        PARENT& parent_;
    };
    
    template <typename U>
    inline hce::yield<result> try_send_(U&& s) {
        std::lock_guard<LOCK> lk(lk_);

        if(closed_flag_) [[unlikely]] { 
            HCE_TRACE_METHOD_BODY("try_send","closed");
            return { result::closed }; 
        } else if(parked_recv_.size()) [[likely]] {
            HCE_TRACE_METHOD_BODY("try_send","done");
            detail::transfer tx(detail::pointer_send<U>,&s);
            parked_recv_.front()->resume((void*)(&tx));
            parked_recv_.pop();

            return { result::success }; 
        } else [[unlikely]] { return { result::failure }; }
    }

    inline void unblock_() {
        std::lock_guard<LOCK> lk(lk_);

        if(!parent_.closed_flag_ && parked_send_.size() && parked_recv_.size()) {
            if(parent_.guard_ && !parent_.guard_.try_claim(parent_.guard_.ctx)) {
                return;
            }

            auto awt = parked_recv_.front();
            parked_recv_.pop();
            awt->resume(awt->destination);
        }
    }

    mutable LOCK lk_;
    bool closed_flag_ = false;
    hce::list<awt<bool>::interface*,ALLOCATOR> parked_send_;
    hce::list<awt<bool>::interface*,ALLOCATOR> parked_recv_;
    detail::guard* guard_ = nullptr;
};

/// buffered interface implementation
template <typename T, typename LOCK=hce::spinlock, typename ALLOCATOR=hce::pool_allocator<T>>
struct buffered : public interface<T> {
    typedef buffered<T,LOCK,ALLOCATOR> self_type;_
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

    buffered(const buffered<T,LOCK,ALLOCATOR>&) = delete;
    buffered(buffered<T,LOCK,ALLOCATOR>&&) = delete;

    inline virtual ~buffered(){ HCE_LOW_DESTRUCTOR(); }

    buffered<T,LOCK,ALLOCATOR>& operator=(const buffered<T,LOCK,ALLOCATOR>&) = delete;
    buffered<T,LOCK,ALLOCATOR>& operator=(buffered<T,LOCK,ALLOCATOR>&&) = delete;

    static inline std::string info_name() { 
        return type::templatize<T,LOCK,ALLOCATOR>("hce::channel::buffered"); 
    }

    inline std::string name() const { 
        return buffered<T,LOCK,ALLOCATOR>::info_name(); 
    }

    inline const std::type_info& type_info() const {
        HCE_TRACE_METHOD_ENTER("type_info");
        return typeid(buffered<T,LOCK,ALLOCATOR>); 
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
            *this, detail::transfer(detail::circular_buffer_send<const T&>,(void*)&s)));
    }

    inline awt<bool> send(T&& s) {
        HCE_LOW_METHOD_ENTER("send",(void*)&s);

        return hce::awt<bool>(new send_interface(
            *this, detail::transfer(detail::circular_buffer_send<T&&>,(void*)&s)));
    }

    inline awt<bool> recv(T& r) {
        HCE_LOW_METHOD_ENTER("recv",(void*)&r);

        return hce::awt<bool>(new recv_interface(*this, (void*)&r));
    }

    inline hce::yield<result> try_send(const T& t) {
        HCE_LOW_METHOD_ENTER("try_send",(void*)&t);
        return try_send_(t);
    }

    inline hce::yield<result> try_send(T&& t) {
        HCE_LOW_METHOD_ENTER("try_send",(void*)&t);
        return try_send_(std::move(t));
    }

    /**
     Buffered receives will succeed, even if the channel is closed, as long 
     as values are available for retrieval from the internal buffer.
     */
    inline hce::yield<result> try_recv(T& r) {
        HCE_LOW_METHOD_ENTER("try_recv",(void*)&r);

        std::lock_guard<LOCK> lk(lk_);

        if(buf_.empty()) [[unlikely]] {
            if(closed_flag_) [[unlikely]] { 
                HCE_TRACE_METHOD_BODY("try_recv","closed");
                return { result::closed }; 
            } else [[likely]] { 
                HCE_TRACE_METHOD_BODY("try_recv","failed");
                return { result::failure }; 
            }
        } else [[likely]] {
            if(parent_.guard_ && 
               !parent_.guard_.try_claim(parent_.guard_.ctx)) [[unlikely]] {
                HCE_TRACE_METHOD_BODY("try_recv","failed");
                return { result::failure }; 
            } else [[likely]] {
                HCE_TRACE_METHOD_BODY("try_recv","done");
                r = std::move(buf_.front());
                buf_.pop();

                if(parked_send_.size()) [[unlikely]] {
                    parked_send_.front()->resume((void*)&buf_);
                    parked_send_.pop();
                }

                // return an awaitable which immediately returns true
                return { result::success }; 
            }
        }
    }

protected:
    inline void set_guard(detail::guard* g) { 
        HCE_LOW_METHOD_ENTER("set_guard");
        std::lock_guard<LOCK> lk(lk_);
        
        guard_ = g; 

        if(guard_) {
            guard_->children.emplace_back(
                this,
                [](void* v) { ((self_type*)v)->unblock_(); }
            );
        }
    }

private:
    typedef buffered<T,LOCK,ALLOCATOR> PARENT;

    struct send_interface : public detail::base_send_interface<LOCK> {
        send_interface(PARENT& p, detail::transfer tx) :
            detail::base_send_interface<LOCK>(p.lk_, tx),
            parent_(p)
        { }

        static inline std::string info_name() {
            return buffered<T,LOCK,ALLOCATOR>::info_name() + 
                   "::send_interface";
        };

        inline std::string name() const { return send_interface::info_name(); }

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
                    detail::transfer tx(detail::circular_buffer_recv<T>,&(parent_.buf_));
                    parent_.parked_recv_.front()->resume((void*)&(tx));
                    parent_.parked_recv_.pop();
                }

                return true;
            }
        }

    private:
        PARENT& parent_;
    };

    struct recv_interface : public detail::base_recv_interface<LOCK> {
        recv_interface(PARENT& p, void* destination) :
            detail::base_recv_interface<LOCK>(p.lk_, destination),
            parent_(p)
        { }

        static inline std::string info_name() {
            return buffered<T,LOCK,ALLOCATOR>::info_name() + 
                   "::recv_interface";
        };

        inline std::string name() const { return recv_interface::info_name(); }

        inline bool on_ready() {
            if(parent_.guard_ && 
               !parent_.guard_.try_claim(parent_.guard_.ctx)) [[unlikely]] 
            {
                HCE_TRACE_METHOD_BODY("recv","blocked");
                parent_.parked_recv_.push_back(this);
                return false;
            } else if(parent_.buf_.empty()) [[unlikely]] {
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
                detail::transfer tx(detail::circular_buffer_recv<T>,&(parent_.buf_));
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
    inline hce::yield<result> try_send_(U&& s) {
        std::lock_guard<LOCK> lk(lk_);

        if(closed_flag_) [[unlikely]] { 
            HCE_TRACE_METHOD_BODY("try_send","closed");
            return { result::closed }; 
        } else if(!buf_.full()) [[likely]] {
            HCE_TRACE_METHOD_BODY("try_send","done");
            buf_.push(std::forward<U>(s));

            if(parked_recv_.size()) [[unlikely]] {
                parked_recv_.front()->resume((void*)&buf_);
                parked_recv_.pop();
            }

            return { result::success }; 
        } else [[unlikely]] { 
            HCE_TRACE_METHOD_BODY("try_send","failed");
            return { result::failure }; 
        }
    }

    inline void unblock_() {
        std::lock_guard<LOCK> lk(lk_);

        if(!parent_.closed_flag_ && parked_send_.size() && !parent_.buf_.full()) {
            if(parent_.guard_ && !parent_.guard_.try_claim(parent_.guard_.ctx)) {
                return;
            }

            auto awt = parked_recv_.front();
            parked_recv_.pop();
            awt->resume((void*)&buf_);
        }
    }

    mutable LOCK lk_;
    bool closed_flag_ = false;
    hce::circular_buffer<T> buf_;
    hce::list<awt<bool>::interface*,ALLOCATOR> parked_send_;
    hce::list<awt<bool>::interface*,ALLOCATOR> parked_recv_;
    detail::guard* guard_ = nullptr;
};

/**
 @brief unlimited interface implementation, with has no max fixed size buffer 
 */
template <typename T, typename LOCK=hce::spinlock, typename ALLOCATOR=hce::pool_allocator<T>>
struct unlimited : public interface<T> {
    typedef unlimited<T,LOCK,ALLOCATOR> self_type;_
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

    unlimited(const unlimited<T,LOCK,ALLOCATOR>&) = delete;
    unlimited(unlimited<T,LOCK,ALLOCATOR>&&) = delete;

    inline virtual ~unlimited(){ HCE_LOW_DESTRUCTOR(); }

    unlimited<T,LOCK,ALLOCATOR>& operator=(const unlimited<T,LOCK,ALLOCATOR>&) = delete;
    unlimited<T,LOCK,ALLOCATOR>& operator=(unlimited<T,LOCK,ALLOCATOR>&&) = delete;

    static inline std::string info_name() { 
        return type::templatize<T,LOCK,ALLOCATOR>("hce::channel::unlimited"); 
    }

    inline std::string name() const { 
        return unlimited<T,LOCK,ALLOCATOR>::info_name(); 
    }

    inline const std::type_info& type_info() const {
        HCE_TRACE_METHOD_ENTER("type_info");
        return typeid(unlimited<T,LOCK,ALLOCATOR>); 
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
            detail::transfer(detail::list_send<const T&,hce::list<T,ALLOCATOR>>,&s)));
    }

    inline awt<bool> send(T&& s) {
        HCE_LOW_METHOD_ENTER("send",(void*)&s);
        return awt<bool>(new send_interface(
            *this, 
            detail::transfer(detail::list_send<T&&,hce::list<T,ALLOCATOR>>,&s)));
    }

    /**
     Buffered receives will succeed, even if the channel is closed, as long 
     as values are available for retrieval from the internal buffer.
     */
    inline awt<bool> recv(T& r) {
        HCE_LOW_METHOD_ENTER("recv",(void*)&r);
        return awt<bool>(new recv_interface(*this, (void*)&r));
    }

    inline hce::yield<result> try_send(const T& t) {
        HCE_LOW_METHOD_ENTER("try_send",(void*)&t);
        return try_send_(t);
    }

    inline hce::yield<result> try_send(T&& t) {
        HCE_LOW_METHOD_ENTER("try_send",(void*)&t);
        return try_send_(std::move(t));
    }

    /**
     Buffered receives will succeed, even if the channel is closed, as long 
     as values are available for retrieval from the internal buffer.
     */
    inline hce::yield<result> try_recv(T& r) {
        HCE_LOW_METHOD_ENTER("try_recv",(void*)&r);

        std::lock_guard<LOCK> lk(lk_);

        if(queue_.empty()) [[unlikely]] {
            if(closed_flag_) [[unlikely]] { 
                HCE_TRACE_METHOD_BODY("try_recv","closed");
                return { result::closed }; 
            } else [[likely]] { 
                HCE_TRACE_METHOD_BODY("try_recv","failed");
                return { result::failure }; 
            }
        } else [[likely]] {
            if(parent_.guard_ && 
               !parent_.guard_.try_claim(parent_.guard_.ctx)) [[unlikely]] {
                HCE_TRACE_METHOD_BODY("try_recv","failed");
                return { result::failure }; 
            } else {
                HCE_TRACE_METHOD_BODY("try_recv","done");
                r = std::move(queue_.front());
                queue_.pop();

                // return an awaitable which immediately returns true
                return { result::success }; 
            }
        }
    }

protected:
    inline void set_guard(detail::guard* g) { 
        HCE_LOW_METHOD_ENTER("set_guard");
        std::lock_guard<LOCK> lk(lk_);
        
        guard_ = g; 

        if(guard_) {
            guard_->children.emplace_back(
                this,
                [](void* v) { ((self_type*)v)->unblock_(); }
            );
        }
    }

private:
    typedef unlimited<T,LOCK,ALLOCATOR> PARENT;

    struct send_interface : public detail::base_send_interface<LOCK> {
        send_interface(PARENT& p, detail::transfer tx) :
            detail::base_send_interface<LOCK>(p.lk_, tx),
            parent_(p)
        { }

        static inline std::string info_name() {
            return unlimited<T,LOCK,ALLOCATOR>::info_name() + 
                   "::send_interface";
        };

        inline std::string name() const { return send_interface::info_name(); }

        inline bool on_ready() { 
            if(parent_.closed_flag_) [[unlikely]] {
                HCE_TRACE_METHOD_BODY("send","closed");
                return true;
            } else [[likely]] {
                HCE_TRACE_METHOD_BODY("send","done");
                this->tx.send(&(parent_.queue_));
                this->success = true;

                if(parent_.parked_recv_.size()) [[unlikely]] {
                    detail::transfer tx(&detail::list_recv<T,hce::list<T,ALLOCATOR>>,&(parent_.queue_));
                    parent_.parked_recv_.front()->resume((void*)&tx);
                    parent_.parked_recv_.pop();
                }

                return true;
            }
        }

    private:
        PARENT& parent_;
    };

    struct recv_interface : public detail::base_recv_interface<LOCK> {
        recv_interface(PARENT& p, void* destination) :
            detail::base_recv_interface<LOCK>(p.lk_, destination),
            parent_(p)
        { }

        static inline std::string info_name() {
            return unlimited<T,LOCK,ALLOCATOR>::info_name() + 
                   "::recv_interface";
        };

        inline std::string name() const { return recv_interface::info_name(); }

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
            } else if(parent_.guard_ && 
                      !parent_.guard_.try_claim(parent_.guard_.ctx)) [[unlikely]] {
                HCE_TRACE_METHOD_BODY("recv","blocked");
                parent_.parked_recv_.push_back(this);
                return false;
            } else [[likely]] {
                HCE_TRACE_METHOD_BODY("recv_","done");
                detail::transfer tx(&detail::list_recv<T,hce::list<T,ALLOCATOR>>,&(parent_.queue_));
                tx.send(this->destination);
                this->success = true;
                return true;
            }
        }

    private:
        PARENT& parent_;
    };
    
    template <typename U>
    inline hce::yield<result> try_send_(U&& s) {
        std::lock_guard<LOCK> lk(lk_);

        if(closed_flag_) [[unlikely]] { 
            HCE_TRACE_METHOD_BODY("try_send","closed");
            return { result::closed }; 
        } else [[likely]] {
            HCE_TRACE_METHOD_BODY("try_send","done");
            queue_.push_back(std::forward<U>(s));

            if(parked_recv_.size()) [[unlikely]] {
                detail::transfer tx(&detail::list_recv<T,hce::list<T,ALLOCATOR>>, &queue_);
                parked_recv_.front()->resume((void*)&tx);
                parked_recv_.pop();
            }

            // return an awaitable which immediately returns true
            return { result::success }; 
        } 
    }

    inline void unblock_() {
        std::lock_guard<LOCK> lk(lk_);

        if(!parent_.closed_flag_ && parked_recv_.size()) {
            if(parent_.guard_ && !parent_.guard_.try_claim(parent_.guard_.ctx)) {
                return;
            }

            auto awt = parked_recv_.front();
            parked_recv_.pop();
            awt->resume((void*)&queue_);
        }
    }

    mutable LOCK lk_;
    bool closed_flag_ = false;
    hce::list<T,ALLOCATOR> queue_;
    hce::list<awt<bool>::interface*,ALLOCATOR> parked_recv_;
    detail::guard* guard_ = nullptr;
};

} // channel

/** 
 @brief communication object which can represent any object which implements channel::interface<T>

 This object is essentially a sanitized 
 `std::shared_ptr<hce::channel::interface<T>>`, with helper construction 
 functions and convenience operators. Its also convient because of its short 
 name/namespace and direct method access, allowing it to be easily used and 
 passed around in coroutines without added template complexity or memory 
 indirection considerations (no `*` or `->` operator usage). Just make copies of 
 an instantiated `hce::chan<T>` and call its methods directly.

 It's useful to point out that implementations of `hce::channel::interface<T>` 
 can actually be used without this shim if desired. An example of using channel 
 implementations directly would be to manage their allocated memory in an 
 `std::unique_ptr<hce::channel::interface<T>>` (or even direct construction) 
 allowing for some different designs, such as global singleton channels which 
 won't allow for context sharing. 
*/
template <typename T>
struct chan : public channel::interface<T> {
    typedef T value_type;

    chan() = default;
    chan(const chan<T>& rhs) = default;
    chan(chan<T>&& rhs) = default;

    inline virtual ~chan(){ }

    chan<T>& operator=(const chan<T>& rhs) = default;
    chan<T>& operator=(chan<T>&& rhs) = default;

    static inline std::string info_name() { 
        return type::templatize<T>("hce::chan"); 
    }

    inline std::string name() const { return chan<T>::info_name(); }

    inline std::string content() const { 
        std::stringstream ss;
        ss << context_.get();
        return ss.str();
    }

    /// return a reference to the shared context
    inline std::shared_ptr<hce::channel::interface<T>>& context() {
        return context_;
    }

    /// return whether channel has an allocated context
    inline explicit operator bool() const {
        HCE_TRACE_METHOD_ENTER("operator bool");
        return (bool)(context_); 
    }

    /**
     @brief construct a chan's context with with one of several possible implementations 

     Constructed chan is:
     - sz == 0: unbuffered implementation
     - sz < 0: unlimited implementation
     - otherwise buffered implementation with a buffer of sz 

     Specify a LOCK type of `hce::lockfree` to make the implementation lockfree 
     (only safe when all instances of the chan are used from the same system 
     thread). Lockfree channels are a *very* fast way to communicate between two 
     coroutines running on the same scheduler (that is, the *same* thread of 
     execution).

     Alternatively, specify a LOCK type of `std::mutex` to potentially improve  
     communication congestion between a large number of system threads (not 
     coroutines running on a small number of threads). `std::mutex` will allow a
     blocked thread to wait on a condition instead of spinlocking.

     When in doubt, use the default `hce::spinlock`, it is quite performant in 
     all but extreme edgecases, because its usages are so brief and coroutine 
     context switching is so fast.

     @param sz the size of implementation buffer 
     @param as optional arguments for implementation constructor
     */
    template <typename LOCK=hce::spinlock, 
              typename ALLOCATOR=hce::pool_allocator<T>,
              typename... As>
    inline chan<T>& construct(int sz=0, As&&... as) {
        HCE_MIN_METHOD_ENTER("construct",sz,as...);

        if(sz == 0) {
            context_ = std::shared_ptr<channel::interface<T>>(
                static_cast<channel::interface<T>*>(
                    new hce::channel::unbuffered<T,LOCK,ALLOCATOR>(std::forward<As>(as)...)));
        } else if(sz < 0) {
            context_ = std::shared_ptr<channel::interface<T>>(
                static_cast<channel::interface<T>*>(
                    new hce::channel::unlimited<T,LOCK,ALLOCATOR>(std::forward<As>(as)...)));
        } else {
            context_ = std::shared_ptr<channel::interface<T>>(
                static_cast<channel::interface<T>*>(
                    new hce::channel::buffered<T,LOCK,ALLOCATOR>(sz, std::forward<As>(as)...)));
        }

        return *this;
    }

    /**
     @brief inline construct a new chan<T> and its context
     @return the constructed chan<T>
     */
    template <typename LOCK=hce::spinlock, 
              typename ALLOCATOR=hce::pool_allocator<T>, 
              typename... As>
    inline static chan<T> make(As&&... as) {
        HCE_MIN_FUNCTION_ENTER(
            chan<T>::info_name() +
            type::templatize<LOCK,ALLOCATOR>("::make"), as...);
        chan<T> ch;
        ch.construct<LOCK,ALLOCATOR>(std::forward<As>(as)...);
        return ch;
    }
       
    inline const std::type_info& type_info() const {
        HCE_TRACE_METHOD_ENTER("type_info");
        return context_->type_info(); 
    }

    inline int size() const {
        HCE_TRACE_METHOD_ENTER("capacity");
        return context_->size(); 
    }

    inline int used() const {
        HCE_TRACE_METHOD_ENTER("size");
        return context_->used(); 
    }

    inline bool closed() const {
        HCE_TRACE_METHOD_ENTER("closed");
        return context_->closed(); 
    }

    inline void close() {
        HCE_MIN_METHOD_ENTER("close");
        return context_->close(); 
    }

    inline hce::awt<bool> send(T&& s) {
        HCE_MIN_METHOD_ENTER("send");
        return context_->send(std::move(s)); 
    }

    inline hce::awt<bool> send(const T& s) {
        HCE_MIN_METHOD_ENTER("send");
        return context_->send(s); 
    }

    inline hce::awt<bool> recv(T& r) {
        HCE_MIN_METHOD_ENTER("recv");
        return context_->recv(r); 
    }

    inline hce::yield<channel::result> try_send(const T& s) {
        HCE_MIN_METHOD_ENTER("try_send");
        return context_->try_send(s);
    }

    inline hce::yield<channel::result> try_send(T&& s) {
        HCE_MIN_METHOD_ENTER("try_send");
        return context_->try_send(std::move(s));
    }

    inline hce::yield<channel::result> try_recv(T& r) {
        HCE_MIN_METHOD_ENTER("try_recv");
        return context_->try_recv(r);
    }

protected:
    inline void set_guard(detail::guard* g) { 
        HCE_MIN_METHOD_ENTER("set_guard");
        context_->set_guard(g);
    }

private:
    std::shared_ptr<channel::interface<T>> context_;
};


/**
 @brief an object capable of negotiating the synchronized receives over one or more hce::chan channels 

 Callbacks registered in this object are not coroutines. It is expected the user 
 lets their callbacks finish and then any further coroutine or channel behavior 
 be executed when `co_await` returns from the `multiplex::select()` operation.

 Example usage in a coroutine:
 ```
 hce::co<void> coro(hce::chan<int> int_ch, hce::chan<std::string> str_ch) {
     int remaining = 2;
     int i;
     std::string s;
     hce::multiplex mux(
        {
            i, // user must preserve the lifetime of variables receiving data
            int_ch, 
            [&]{ 
                // treat these as standard non-coroutine function calls
                std::cout << "received int: " << i << std::endl;
                --remaining;
            }
        },
        {
            s,
            str_ch,
            [&]{
                std::cout << "received std::string: " << s << std::endl;
                --remaining;
            }
        });
     
     while(remaining) {
        co_await mux.select();
        // do any other coroutine behavior here...
     }

     co_await mux.finish();
 }
 ```
 */
struct multiplex : public printable {
    using callback = std::function<void()>;

private:
    // element interface
    struct interface {
        typedef void(*deleter)(interface*);

        virtual ~interface();
        virtual void set_guard(detail::guard*) = 0; // set the channel guard
        virtual void launch() = 0; // launch forwarding operation
        virtual void close() = 0;  // close the channel and end forwarding
        virtual void* child() = 0; // get the child context
       
        multiplex* parent = nullptr; // owning multiplex 
        size_t idx;                  // index of this element
        hce::awt<void> awt;           // launched coroutine awaitable
        callback call;               // registered callback
    };

    // type erased interface with packaged deleter
    struct erased {
        erased() = delete;
        erased(const erased&) = delete;
        erased(multiplex::interface* i, multiplex::interface::deleter d);
        erased(erased&&);
        ~erased();

        erased& operator=(const erased&) = delete;
        erased& operator=(erased&&);

        multiplex::interface*         interface = nullptr;
        multiplex::interface::deleter deleter = nullptr;
    };

    struct guard_mgr {
        std::atomic<bool> claimed{false};
        std::atomic<bool> finished{false};
        detail::guard guard;
        std::size_t current_idx = 0;

        // unblock all guarded children
        void unblock();

        // attempt to claim the guard
        bool try_claim();

        // release theguard
        void release();
    };

public:
    /**
     @brief an object which connects a channel to a callback
     */
    struct element {
        /**
         The user must guarantee the argument value T stays in existence.

         @param t the value to assign over the channel. 
         @param ch channel to received from
         @param cb callback to execute when t is received
         */
        template <typename T>
        element(T& t, hce::chan<T> ch, callback cb) :
            e(make_interface(t, std::move(ch), std::move(cb)), &deleter<T>)
        { }

    private:
        template <typename T>
        static interface* make_interface(T& t, CHAN&& ch, callback&& cb) {
            interface* i = hce::allocate<impl<T>>(1);
            ::new (static_cast<void*>(i)) impl<T>(t, std::move(ch));
            i->call = std::move(cb);
            return i;
        }
        
        template <typename T>
        static inline deleter(interface i) {
            if(i) {
                i->~interface();
                hce::deallocate(i); 
            }
        }

        erased e;
        friend struct multiplex;
    };

    /**
     @brief launch a multiplex of elements on construction
     */
    template <typename Es...>
    multiplex(Es&&... es) :
        guard_mgr_(channel::detail::guard{
            channel::detail::guard::parent_t{ 
                this, 
                [](void* v) -> bool { 
                    return ((multiplex::guard*)v)->try_claim();
                }
            },
            channel::detail::guard::child_t{}
        })
    {
        launch(std::forward<Es>(es)...);
    }

    /// WARNING: finish() must be called or this will throw a fatal
    ~multiplex();

    static std::string info_name();
    std::string name() const;

    /**
     @brief launch a multiplex of elements  

     Start element's operations to select between channels.

     Can be called more than once to multiplex additional channels.
     */
    template <typename Es...>
    void launch(Es&&... es) {
        if(!received_ || received_.closed()) {
            received_ = hce::chan<size_t>::make();
        }

        elements_.reserve(vec.size() + sizeof...(es)); 
        launch_(std::forward<Es>(es)...);
    }

    /**
     @brief await until some callback gets called or multiplexing is finish()ed 

     No callbacks will execute once this method has returned, until it is called 
     again.

     @return await the result to get whether the multiplex succeeded
     */
    hce::awt<bool> select();

    /**
     @brief end multiplexing 

     This unregisters this object from associated element channels.

     @return await the result to finish multiplexing operations
     */
    hce::awt<void> finish();

private:
    struct result_t {
        size_t idx = 0;
        bool done = false;
    };
    
    // element implementation
    template <typename T>
    struct impl : interface {
        virtual ~impl() { }

        template <typename CHAN>
        impl(T& t, CHAN&& c) :
            value(t),
            ch(std::forward<CHAN>(c))
        { }

        inline void set_guard(detail::guard* g) { ch.set_guard(g); }
        inline void launch() { awt = hce::schedule(forwarder(this)); }
        inline void close() { ch.close(); }
        inline void* child() { return ch.context().get(); }; 

        // A single shot coroutine to receive over the multiplexed channel
        static inline hce::co<void> forwarder(impl<T>* self) {
            result_t result;
            result.idx = self->idx;

            // recv blocks until:
            // A) a send succeeds in acquiring the guard lock 
            // B) the parent finishes processing the callback and releases the claim
            while(co_await self->ch.recv(value)) {
                // we have the guard claimed, notify the selector 
                co_await parent->received_.send(result);
            } 

            // notify we died
            result.done = true;
            co_await parent->received_.send(result);
        }

        T& value;
        hce::chan<t> ch;
        hce::awt<void> awt;
    };

    hce::co<void> selector_(hce::multiplex*);

    void launch_(); 

    template <typename... Es>
    void launch_(element e, Es&&... es) {
        elements_.push_back(std::move(e));
        auto& cur = elements_.back();
        cur.parent = this;
        cur.idx = elements.size() - 1;
        // increment the children vector in sync with the elements_ vector
        cur.e.interface->set_guard(guard_mgr_.guard);
        launch_(std::forward<Es>(es)...);
    }

    void unmanage_(size_t idx);

    guard_mgr guard_mgr_;
    // the indexes of this should match that of guard_mgr::guard::children
    std::vector<element> elements_; 
    hce::chan<result_t> received_;
};

}
