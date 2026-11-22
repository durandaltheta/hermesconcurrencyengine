#pragma once
//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
// c++
#include <utility>
#include <vector>

// local
#include "hce/utility.hpp"
#include "hce/logging.hpp"
#include "hce/atomic.hpp"
#include "hce/alloc.hpp"
#include "hce/circular_buffer.hpp"
#include "hce/scheduler.hpp"

namespace hce {
namespace channel {

// Used to specially control blocking behavior of the channel interface. 
struct guard {
    /* 
     All values here are set by a parent object. Functions in this block are 
     always passed the parent_t::ctx.
     */
    struct parent_t {
        void* ctx = nullptr; // a context pointer (will already be set)

        /*
         Called by the interface *every* time a recv occurs. Returns true if the 
         recv can proceed, and false if the channel must block.

         Accepts the ctx context pointer.
         */
        bool (*try_claim_recv)(void*) = nullptr;
    };


    /*
     All values must be assigned by the interface during set_guard(). Functions 
     in this block are always passed the specific child_t::ctx.
     */
    struct child_t {
        void* ctx = nullptr; // pointer to the child implementation

        /*
         Called by the parent when the channel must become unblocked. Passes in 
         self as the argument.
         */
        void (*unblock)(void*) = nullptr;

        /*
         Called by the parent when the parent context needs to be removed from 
         the child.
         */
        void (*remove)(void*) = nullptr;
    };

    /*
     Parent context comes pre-assigned.
     */
    const parent_t parent;

    /*
     The channel is responsible to append their child_t to this vector during 
     a call to set_guard().
     */
    std::vector<child_t> children;
};

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

    /// register a guard object
    virtual void set_guard(guard*) = 0;
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

} // detail 
} // channel 
} // hce
