//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#ifndef HERMES_COROUTINE_ENGINE_CHANNEL
#define HERMES_COROUTINE_ENGINE_CHANNEL

// c++
#include <typeinfo>
#include <mutex>

// local
#include "utility.hpp"
#include "logging.hpp"
#include "atomic.hpp"
#include "memory.hpp"
#include "alloc.hpp"
#include "circular_buffer.hpp"
#include "list.hpp"
#include "coroutine.hpp"
#include "scheduler.hpp"

namespace hce {
namespace channel {

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

template <typename Lock>
struct base_send_interface : 
    public hce::scheduler::reschedule<
        hce::awaitable::lockable<
            Lock,
            awt_interface<bool>>>
{
    base_send_interface(Lock& lk, transfer t) : 
        hce::scheduler::reschedule<
            hce::awaitable::lockable<
                Lock,
                awt_interface<bool>>>(
                    lk,
                    hce::awaitable::await::policy::defer_lock,
                    hce::awaitable::resumed::policy::locked,
                    hce::awaitable::resume::policy::lock_exempt),
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

template <typename Lock>
struct base_recv_interface : 
    public hce::scheduler::reschedule<
        hce::awaitable::lockable<
            Lock,
            awt_interface<bool>>>
{
    base_recv_interface(Lock& lk, void* d) : 
        hce::scheduler::reschedule<
            hce::awaitable::lockable<
                Lock,
                awt_interface<bool>>>(
                    lk,
                    hce::awaitable::await::policy::defer_lock,
                    hce::awaitable::resumed::policy::locked,
                    hce::awaitable::resume::policy::lock_exempt),
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

/*
 Template awaitable deleters which handle the subtle locking mechanics required 
 to reuse channel awaitable allocated memory while still properly destructing 
 instances. This is intended to be returned from the hce::awaitable::interface 
 implementation's virtual overload of hce::awaitable::interface::deleter().
 */
struct deleter {
    template <typename Sender>
    static void sender(hce::awaitable::interface* i) {
        Sender* si = (Sender*)i;
        auto& parent = si->parent_;
        si->~Sender();
        parent.send_alloc_.deallocate(si,1);
        parent.lk_.unlock();
    }

    template <typename Receiver>
    static void receiver(hce::awaitable::interface* i) {
        Receiver* ri = (Receiver*)i;
        auto& parent = ri->parent_;
        ri->~Receiver();
        parent.recv_alloc_.deallocate(ri,1);
        parent.lk_.unlock();
    }
};

}

/// unbuffered interface implementation
template <typename T, typename Lock=hce::spinlock>
struct unbuffered : public interface<T> {
    typedef T value_type;
    typedef hce::pool_allocator<T> Allocator;

    unbuffered(size_t expected_sender_count = 1, size_t expected_receiver_count = 1) :
        // allocator pool size reflects the expectation that the common usecase 
        // is 1 sender and 1 receiver 
        send_alloc_(expected_sender_count),
        recv_alloc_(expected_receiver_count),
        parked_send_(Allocator(expected_sender_count)),
        parked_recv_(Allocator(expected_receiver_count))
    {
        HCE_LOW_CONSTRUCTOR(); 
    }

    unbuffered(const unbuffered<T,Lock>&) = delete;
    unbuffered(unbuffered<T,Lock>&&) = delete;
    
    inline virtual ~unbuffered(){ HCE_LOW_DESTRUCTOR(); }

    unbuffered<T,Lock>& operator=(const unbuffered<T,Lock>&) = delete;
    unbuffered<T,Lock>& operator=(unbuffered<T,Lock>&&) = delete;

    static inline std::string info_name() { 
        return type::templatize<T,Lock>("hce::channel::unbuffered"); 
    }

    inline std::string name() const { 
        return unbuffered<T,Lock>::info_name(); 
    }

    inline const std::type_info& type_info() const {
        HCE_TRACE_METHOD_ENTER("type_info");
        return typeid(unbuffered<T,Lock>); 
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

        std::lock_guard<Lock> lk(lk_);
        return closed_flag_;
    }

    inline void close() {
        HCE_LOW_METHOD_ENTER("close");

        std::lock_guard<Lock> lk(lk_);
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
        send_interface* si = send_alloc_.allocate(1);
        ::new(si) send_interface(*this, detail::transfer(detail::pointer_send<const T&>,&s));
        return hce::awt<bool>(si);
    }

    inline awt<bool> send(T&& s) {
        HCE_LOW_METHOD_ENTER("send",(void*)&s);

        send_interface* si = send_alloc_.allocate(1);
        ::new(si) send_interface(*this, detail::transfer(detail::pointer_send<T&&>,&s));
        return hce::awt<bool>(si);
    }

    inline awt<bool> recv(T& r) {
        HCE_LOW_METHOD_ENTER("recv",(void*)&r);

        recv_interface* ri = recv_alloc_.allocate(1);
        ::new(ri) recv_interface(*this, (void*)&r);
        return hce::awt<bool>(ri);
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

        std::lock_guard<Lock> lk(lk_);

        if(closed_flag_) [[unlikely]] { 
            return { result::closed }; 
        } else if(parked_send_.size()) [[likely]] {
            parked_send_.front()->resume((void*)&r);

            // return an awaitable which immediately returns true
            return { result::success }; 
        } else [[unlikely]] { return { result::failure }; }
    }

private:
    typedef unbuffered<T,Lock> PARENT;

    struct send_interface : public detail::base_send_interface<Lock> {
        send_interface(PARENT& p, detail::transfer tx) :
            detail::base_send_interface<Lock>(p.lk_, tx),
            parent_(p)
        { 
            HCE_MIN_CONSTRUCTOR();
        }

        virtual ~send_interface() {
            HCE_MIN_DESTRUCTOR();
        }

        static inline std::string info_name() {
            return unbuffered<T,Lock>::info_name() + 
                   "::send_interface";
        };

        inline std::string name() const { return send_interface::info_name(); }

        inline hce::awaitable::interface::deleter_t deleter() {
            return detail::deleter::sender<send_interface>;
        }

        inline void on_ready() {
            if(parent_.closed_flag_) [[unlikely]] {
                HCE_TRACE_METHOD_BODY("send","closed");
                this->set_ready();
            } else if(parent_.parked_recv_.size()) [[likely]] {
                HCE_TRACE_METHOD_BODY("send","done");
                parent_.parked_recv_.front()->resume((void*)&(this->tx));
                parent_.parked_recv_.pop();
                this->success = true;
                this->set_ready();
            } else [[unlikely]] {
                HCE_TRACE_METHOD_BODY("send","blocked");
                parent_.parked_send_.push_back(this);
            }
        }

    private:
        PARENT& parent_;
        friend detail::deleter;
    };

    struct recv_interface : public detail::base_recv_interface<Lock> {
        recv_interface(PARENT& p, void* destination) :
            detail::base_recv_interface<Lock>(p.lk_, destination),
            parent_(p)
        { 
            HCE_MIN_CONSTRUCTOR();
        }

        virtual ~recv_interface() {
            HCE_MIN_DESTRUCTOR();
        }

        static inline std::string info_name() {
            return unbuffered<T,Lock>::info_name() + 
                   "::recv_interface";
        };

        inline std::string name() const { return recv_interface::info_name(); }

        inline hce::awaitable::interface::deleter_t deleter() {
            return detail::deleter::receiver<recv_interface>;
        }

        inline void on_ready() {
            if(parent_.closed_flag_) [[unlikely]] { 
                HCE_TRACE_METHOD_BODY("recv","closed");
                this->set_ready();
            } else if(parent_.parked_send_.size()) [[likely]] {
                HCE_TRACE_METHOD_BODY("recv","resume");
                parent_.parked_send_.front()->resume(this->destination);
                parent_.parked_send_.pop();
                this->success = true;
                this->set_ready();
            } else [[unlikely]] {
                HCE_TRACE_METHOD_BODY("recv","block for transfer");
                parent_.parked_recv_.push_back(this);
            }
        }

    private:
        PARENT& parent_;
        friend detail::deleter;
    };
    
    template <typename U>
    inline hce::yield<result> try_send_(U&& s) {
        std::lock_guard<Lock> lk(lk_);

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

    mutable Lock lk_;
    bool closed_flag_ = false;
    typename Allocator::template rebind<send_interface>::other send_alloc_; 
    typename Allocator::template rebind<recv_interface>::other recv_alloc_; 
    hce::list<awt<bool>::interface*,Allocator> parked_send_;
    hce::list<awt<bool>::interface*,Allocator> parked_recv_;
    friend detail::deleter;
};

/// buffered interface implementation
template <typename T, typename Lock=hce::spinlock>
struct buffered : public interface<T> {
    typedef T value_type;
    typedef hce::pool_allocator<T> Allocator;

    buffered(int buffer_size, 
             size_t expected_sender_count = 1, 
             size_t expected_receiver_count = 1) : 
        buf_(buffer_size ? (size_t)buffer_size : (size_t)1),
        send_alloc_(expected_sender_count),
        recv_alloc_(expected_receiver_count),
        parked_send_(Allocator(expected_sender_count)),
        parked_recv_(Allocator(expected_receiver_count))
    { 
        HCE_LOW_CONSTRUCTOR();
    }

    buffered(const buffered<T,Lock>&) = delete;
    buffered(buffered<T,Lock>&&) = delete;

    inline virtual ~buffered(){ HCE_LOW_DESTRUCTOR(); }

    buffered<T,Lock>& operator=(const buffered<T,Lock>&) = delete;
    buffered<T,Lock>& operator=(buffered<T,Lock>&&) = delete;

    static inline std::string info_name() { 
        return type::templatize<T,Lock>("hce::channel::buffered"); 
    }

    inline std::string name() const { 
        return buffered<T,Lock>::info_name(); 
    }

    inline const std::type_info& type_info() const {
        HCE_TRACE_METHOD_ENTER("type_info");
        return typeid(buffered<T,Lock>); 
    }

    inline int size() const {
        HCE_MIN_METHOD_ENTER("size");

        std::lock_guard<Lock> lk(lk_);
        return (int)buf_.size();
    }

    inline int used() const {
        HCE_MIN_METHOD_ENTER("used");

        std::lock_guard<Lock> lk(lk_);
        return (int)buf_.used();
    }

    inline bool closed() const {
        HCE_MIN_METHOD_ENTER("closed");

        std::lock_guard<Lock> lk(lk_);
        return closed_flag_;
    }

    inline void close() {
        HCE_LOW_METHOD_ENTER("close");

        std::lock_guard<Lock> lk(lk_);

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

        send_interface* si = send_alloc_.allocate(1);
        ::new(si) send_interface(
            *this, 
            detail::transfer(
                detail::circular_buffer_send<const T&>,
                (void*)&s));
        return hce::awt<bool>(si);
    }

    inline awt<bool> send(T&& s) {
        HCE_LOW_METHOD_ENTER("send",(void*)&s);

        send_interface* si = send_alloc_.allocate(1);
        ::new(si) send_interface(
            *this, 
            detail::transfer(
                detail::circular_buffer_send<T&&>,
                (void*)&s));
        return hce::awt<bool>(si);
    }

    inline awt<bool> recv(T& r) {
        HCE_LOW_METHOD_ENTER("recv",(void*)&r);

        recv_interface* ri = recv_alloc_.allocate(1);
        ::new(ri) recv_interface(*this, (void*)&r);
        return hce::awt<bool>(ri);
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

        std::lock_guard<Lock> lk(lk_);

        if(buf_.empty()) [[unlikely]] {
            if(closed_flag_) [[unlikely]] { 
                HCE_TRACE_METHOD_BODY("try_recv","closed");
                return { result::closed }; 
            } else [[likely]] { 
                HCE_TRACE_METHOD_BODY("try_recv","failed");
                return { result::failure }; 
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
            return { result::success }; 
        }
    }

private:
    typedef buffered<T,Lock> PARENT;

    struct send_interface : public detail::base_send_interface<Lock> {
        send_interface(PARENT& p, detail::transfer tx) :
            detail::base_send_interface<Lock>(p.lk_, tx),
            parent_(p)
        { 
            HCE_MIN_CONSTRUCTOR();
        }

        virtual ~send_interface() {
            HCE_MIN_DESTRUCTOR();
        }

        static inline std::string info_name() {
            return buffered<T,Lock>::info_name() + 
                   "::send_interface";
        };

        inline std::string name() const { return send_interface::info_name(); }

        inline hce::awaitable::interface::deleter_t deleter() {
            return detail::deleter::sender<send_interface>;
        }

        inline void on_ready() {
            if(parent_.closed_flag_) [[unlikely]] {
                HCE_TRACE_METHOD_BODY("send","closed");
                this->set_ready();
            } else if(parent_.buf_.full()) [[unlikely]] {
                HCE_TRACE_METHOD_BODY("send","blocked");
                parent_.parked_send_.push_back(this);
            } else [[likely]] {
                HCE_TRACE_METHOD_BODY("send","done");
                this->tx.send(&(parent_.buf_));
                this->success = true;

                if(parent_.parked_recv_.size()) [[unlikely]] {
                    detail::transfer tx(detail::circular_buffer_recv<T>,&(parent_.buf_));
                    parent_.parked_recv_.front()->resume((void*)&(tx));
                    parent_.parked_recv_.pop();
                }

                this->set_ready();
            }
        }

    private:
        PARENT& parent_;
        friend detail::deleter;
    };

    struct recv_interface : public detail::base_recv_interface<Lock> {
        recv_interface(PARENT& p, void* destination) :
            detail::base_recv_interface<Lock>(p.lk_, destination),
            parent_(p)
        { 
            HCE_MIN_CONSTRUCTOR();
        }

        virtual ~recv_interface() {
            HCE_MIN_DESTRUCTOR();
        }

        static inline std::string info_name() {
            return buffered<T,Lock>::info_name() + 
                   "::recv_interface";
        };

        inline std::string name() const { return recv_interface::info_name(); }

        inline hce::awaitable::interface::deleter_t deleter() {
            return detail::deleter::receiver<recv_interface>;
        }

        inline void on_ready() {
            if(parent_.buf_.empty()) [[unlikely]] {
                if(parent_.closed_flag_ ) [[unlikely]] {
                    HCE_TRACE_METHOD_BODY("recv","closed");
                    this->set_ready();
                } else [[likely]] {
                    HCE_TRACE_METHOD_BODY("recv","blocked");
                    parent_.parked_recv_.push_back(this);
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

                this->set_ready();
            }
        }

    private:
        PARENT& parent_;
        friend detail::deleter;
    };

    template <typename U>
    inline hce::yield<result> try_send_(U&& s) {
        std::lock_guard<Lock> lk(lk_);

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

    mutable Lock lk_;
    bool closed_flag_ = false;
    hce::circular_buffer<T> buf_;
    typename Allocator::template rebind<send_interface>::other send_alloc_; 
    typename Allocator::template rebind<recv_interface>::other recv_alloc_; 
    hce::list<awt<bool>::interface*,Allocator> parked_send_;
    hce::list<awt<bool>::interface*,Allocator> parked_recv_;
    friend detail::deleter;
};

/// unlimited interface implementation
template <typename T, typename Lock=hce::spinlock>
struct unlimited : public interface<T> {
    typedef T value_type;
    typedef hce::pool_allocator<T> Allocator;

    unlimited(size_t expected_sender_count = 1, size_t expected_receiver_count = 1) :
        send_alloc_(expected_sender_count),
        recv_alloc_(expected_receiver_count),
        parked_recv_(Allocator(expected_receiver_count))
    { 
        HCE_LOW_CONSTRUCTOR();
    }

    unlimited(const unlimited<T,Lock>&) = delete;
    unlimited(unlimited<T,Lock>&&) = delete;

    inline virtual ~unlimited(){ HCE_LOW_DESTRUCTOR(); }

    unlimited<T,Lock>& operator=(const unlimited<T,Lock>&) = delete;
    unlimited<T,Lock>& operator=(unlimited<T,Lock>&&) = delete;

    static inline std::string info_name() { 
        return type::templatize<T,Lock>("hce::channel::unlimited"); 
    }

    inline std::string name() const { 
        return unlimited<T,Lock>::info_name(); 
    }

    inline const std::type_info& type_info() const {
        HCE_TRACE_METHOD_ENTER("type_info");
        return typeid(unlimited<T,Lock>); 
    }

    inline int size() const {
        HCE_MIN_METHOD_ENTER("size");
        return -1;
    }

    inline int used() const {
        HCE_MIN_METHOD_ENTER("used");

        std::lock_guard<Lock> lk(lk_);
        return (int)queue_.size();
    }

    inline bool closed() const {
        HCE_MIN_METHOD_ENTER("closed");

        std::lock_guard<Lock> lk(lk_);
        return closed_flag_;
    }

    inline void close() {
        HCE_LOW_METHOD_ENTER("close");

        std::lock_guard<Lock> lk(lk_);

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

        send_interface* si = send_alloc_.allocate(1);
        ::new(si) send_interface(
            *this, 
            detail::transfer(detail::list_send<const T&,hce::list<T,Allocator>>,&s));
        return awt<bool>(si);
    }

    inline awt<bool> send(T&& s) {
        HCE_LOW_METHOD_ENTER("send",(void*)&s);

        send_interface* si = send_alloc_.allocate(1);
        ::new(si) send_interface(
            *this, 
            detail::transfer(detail::list_send<T&&,hce::list<T,Allocator>>,&s));
        return awt<bool>(si);
    }

    /**
     Buffered receives will succeed, even if the channel is closed, as long 
     as values are available for retrieval from the internal buffer.
     */
    inline awt<bool> recv(T& r) {
        HCE_LOW_METHOD_ENTER("recv",(void*)&r);

        recv_interface* ri = recv_alloc_.allocate(1);
        ::new(ri) recv_interface(*this, (void*)&r);
        return hce::awt<bool>(ri);
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

        std::lock_guard<Lock> lk(lk_);

        if(queue_.empty()) [[unlikely]] {
            if(closed_flag_) [[unlikely]] { 
                HCE_TRACE_METHOD_BODY("try_recv","closed");
                return { result::closed }; 
            } else [[likely]] { 
                HCE_TRACE_METHOD_BODY("try_recv","failed");
                return { result::failure }; 
            }
        } else [[likely]] {
            HCE_TRACE_METHOD_BODY("try_recv","done");
            r = std::move(queue_.front());
            queue_.pop();

            // return an awaitable which immediately returns true
            return { result::success }; 
        }
    }

private:
    typedef unlimited<T,Lock> PARENT;

    struct send_interface : public detail::base_send_interface<Lock> {
        send_interface(PARENT& p, detail::transfer tx) :
            detail::base_send_interface<Lock>(p.lk_, tx),
            parent_(p)
        { 
            HCE_MIN_CONSTRUCTOR();
        }

        virtual ~send_interface() {
            HCE_MIN_DESTRUCTOR();
        }

        static inline std::string info_name() {
            return unlimited<T,Lock>::info_name() + 
                   "::send_interface";
        };

        inline std::string name() const { return send_interface::info_name(); }

        inline hce::awaitable::interface::deleter_t deleter() {
            return detail::deleter::sender<send_interface>;
        }

        inline void on_ready() { 
            if(parent_.closed_flag_) [[unlikely]] {
                HCE_TRACE_METHOD_BODY("send","closed");
                this->set_ready();
            } else [[likely]] {
                HCE_TRACE_METHOD_BODY("send","done");
                this->tx.send(&(parent_.queue_));
                this->success = true;

                if(parent_.parked_recv_.size()) [[unlikely]] {
                    detail::transfer tx(&detail::list_recv<T,hce::list<T,Allocator>>,&(parent_.queue_));
                    parent_.parked_recv_.front()->resume((void*)&tx);
                    parent_.parked_recv_.pop();
                }

                this->set_ready();
            }
        }

    private:
        PARENT& parent_;
        friend detail::deleter;
    };

    struct recv_interface : public detail::base_recv_interface<Lock> {
        recv_interface(PARENT& p, void* destination) :
            detail::base_recv_interface<Lock>(p.lk_, destination),
            parent_(p)
        { 
            HCE_MIN_CONSTRUCTOR();
        }

        virtual ~recv_interface() {
            HCE_MIN_DESTRUCTOR();
        }

        static inline std::string info_name() {
            return unlimited<T,Lock>::info_name() + 
                   "::recv_interface";
        };

        inline std::string name() const { return recv_interface::info_name(); }

        inline hce::awaitable::interface::deleter_t deleter() {
            return detail::deleter::receiver<recv_interface>;
        }

        inline void on_ready() {
            if(parent_.queue_.empty()) [[unlikely]] {
                if(parent_.closed_flag_ ) [[unlikely]] {
                    HCE_TRACE_METHOD_BODY("recv","closed");
                    this->set_ready();
                } else [[likely]] {
                    HCE_TRACE_METHOD_BODY("recv","blocked");
                    parent_.parked_recv_.push_back(this);
                }
            } else [[likely]] {
                HCE_TRACE_METHOD_BODY("recv_","done");
                detail::transfer tx(&detail::list_recv<T,hce::list<T,Allocator>>,&(parent_.queue_));
                tx.send(this->destination);
                this->success = true;
                this->set_ready();
            }
        }

    private:
        PARENT& parent_;
        friend detail::deleter;
    };
    
    template <typename U>
    inline hce::yield<result> try_send_(U&& s) {
        std::lock_guard<Lock> lk(lk_);

        if(closed_flag_) [[unlikely]] { 
            HCE_TRACE_METHOD_BODY("try_send","closed");
            return { result::closed }; 
        } else [[likely]] {
            HCE_TRACE_METHOD_BODY("try_send","done");
            queue_.push_back(std::forward<U>(s));

            if(parked_recv_.size()) [[unlikely]] {
                detail::transfer tx(&detail::list_recv<T,hce::list<T,Allocator>>, &queue_);
                parked_recv_.front()->resume((void*)&tx);
                parked_recv_.pop();
            }

            // return an awaitable which immediately returns true
            return { result::success }; 
        } 
    }

    mutable Lock lk_;
    bool closed_flag_ = false;
    hce::list<T,Allocator> queue_;
    typename Allocator::template rebind<send_interface>::other send_alloc_; 
    typename Allocator::template rebind<recv_interface>::other recv_alloc_; 

    // send() never blocks, so only has parked recv queue
    hce::list<awt<bool>::interface*,Allocator> parked_recv_;
    friend detail::deleter;
};

}

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

     Specify a Lock type of `hce::lockfree` to make the implementation lockfree 
     (only safe when all instances of the chan are used from the same system 
     thread). Lockfree channels are a *very* fast way to communicate between two 
     coroutines running on the same scheduler (that is, the *same* thread of 
     execution).

     Alternatively, specify a Lock type of `std::mutex` to potentially improve  
     communication congestion between a large number of system threads (not 
     coroutines running on a small number of threads). `std::mutex` will allow a
     blocked thread to wait on a condition instead of spinlocking.

     When in doubt, use the default `hce::spinlock`, it is quite performant in 
     all but extreme edgecases, because its usages are so brief and coroutine 
     context switching is so fast.

     @param sz the size of implementation buffer 
     @param as optional arguments for implementation constructor
     */
    template <typename Lock=hce::spinlock, typename... As>
    inline chan<T>& construct(int sz=0, As&&... as) {
        HCE_MIN_METHOD_ENTER("construct",sz,as...);

        if(sz == 0) {
            context_ = std::shared_ptr<channel::interface<T>>(
                static_cast<channel::interface<T>*>(
                    new hce::channel::unbuffered<T,Lock>(std::forward<As>(as)...)));
        } else if(sz < 0) {
            context_ = std::shared_ptr<channel::interface<T>>(
                static_cast<channel::interface<T>*>(
                    new hce::channel::unlimited<T,Lock>(std::forward<As>(as)...)));
        } else {
            context_ = std::shared_ptr<channel::interface<T>>(
                static_cast<channel::interface<T>*>(
                    new hce::channel::buffered<T,Lock>(sz, std::forward<As>(as)...)));
        }

        return *this;
    }

    /**
     @brief inline construct a new chan<T> and its context
     @return the constructed chan<T>
     */
    template <typename Lock=hce::spinlock, typename... As>
    inline static chan<T> make(As&&... as) {
        HCE_MIN_FUNCTION_ENTER(
            chan<T>::info_name() +
            type::templatize<Lock>("::make"), as...);
        chan<T> ch;
        ch.construct<Lock>(std::forward<As>(as)...);
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

private:
    std::shared_ptr<channel::interface<T>> context_;
};

}

#endif
