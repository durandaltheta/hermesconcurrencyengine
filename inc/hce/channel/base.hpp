#pragma once
//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
// c++
#include <utility>
#include <atomic>
#include <map>
#include <optional>

// local
#include "hce/utility.hpp"
#include "hce/atomic.hpp"
#include "hce/printable.hpp"
#include "hce/fatal.hpp"
#include "hce/list.hpp"
#include "hce/circular_buffer.hpp"
#include "hce/scheduler.hpp"

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
struct interface : printable {
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

    /**
     @brief receive a value
     @return awaitable returning true on success, else false
     */
    virtual hce::awt<bool> recv(T& t) = 0;

    /// attempt to send a lvalue copy
    virtual hce::yield<channel::result> try_send(const T& t) = 0;

    /// attempt to send a rvalue copy
    virtual hce::yield<channel::result> try_send(T&& t) = 0;

    /**
     @brief attempt to receive a value
     @return awaitable returning true a result 
     */
    virtual hce::yield<channel::result> try_recv(T& t) = 0;
};

namespace detail {

template <typename T>
inline void pointer_send(void* destination, void* source) {
    typedef unqualified<T> U;

    // compiler knows how to select lvalue or rvalue copy since all necessary 
    // type information is available
    *((U*)destination) = std::forward<T>(*((U*)(source))); 
}

template <typename T>
inline void circular_buffer_send(void* destination, void* source) {
    typedef unqualified<T> U;
    typedef hce::circular_buffer<U> BUFFER;
    // compiler knows how to select lvalue or rvalue copy since all necessary 
    // type information is available
    ((BUFFER*)destination)->emplace(std::forward<T>(*((U*)(source)))); 
}

template <typename T>
inline void circular_buffer_recv(void* destination, void* source) {
    typedef hce::circular_buffer<T> BUFFER;
    // is always a move operation
    *((T*)destination) = std::move(((BUFFER*)source)->front());
    ((BUFFER*)source)->pop();
}

template <typename T, typename LIST>
inline void list_send(void* destination, void* source) {
    typedef unqualified<T> U;
    // compiler knows how to select lvalue or rvalue copy since all necessary 
    // type information is available
    ((LIST*)destination)->emplace_back(std::forward<T>(*((U*)(source)))); 
}

template <typename T, typename LIST>
inline void list_recv(void* destination, void* source) {
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
    hce::scheduler::reschedule,
    hce::awaitable::lockable<
        LOCK,
        awt_interface<bool>>
{
    base_send_interface(LOCK& lk, transfer t) : 
        hce::awaitable::lockable<
            LOCK,
            awt_interface<bool>>(
                lk,
                hce::awaitable::await::policy::defer,
                hce::awaitable::resume::policy::no_lock),
        tx(t)
    { }

    inline void on_resume(void* m) {
        if(m) [[likely]] {
            // m is destination
            tx.send(m);
            success = true;
        }
    }

    inline bool get_result() { 
        return success; 
    }

    bool success = false;
    transfer tx;
};

template <typename LOCK>
struct base_recv_interface : 
    hce::scheduler::reschedule,
    hce::awaitable::lockable<
        LOCK,
        awt_interface<bool>>
{
    base_recv_interface(LOCK& lk, void* d) : 
        hce::awaitable::lockable<
            LOCK,
            awt_interface<bool>>(
                lk,
                hce::awaitable::await::policy::defer,
                hce::awaitable::resume::policy::no_lock),
        destination(d)
    { }

    inline void on_resume(void* m) {
        if(m) [[likely]] {
            // m is transfer struct
            ((transfer*)m)->send(destination);
            success = true;
        }
    }

    inline bool get_result() { 
        return success; 
    }

    bool success = false;
    void* destination = nullptr;
};

} // detail 
} // channel 
} // hce
