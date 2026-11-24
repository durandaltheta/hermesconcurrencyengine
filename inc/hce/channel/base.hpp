#pragma once
//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
// c++
#include <utility>
#include <atomic>
#include <vector>

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

/// standardized channel awaitable list
using list = hce::list<hce::awt<bool>::interface*, hce::pool_allocator<T>>;

namespace select {

/// group used for managing selects
struct group {
    bool try_claim(int index); ///< try and claim at some index
    bool is_claimed() const; ///< see if someone won
    int winner_index() const; ///< get the winning index
    void reset(); ///< reset everything

    std::atomic<bool> claimed{false};
    std::atomic<int>  winner{-1};
};

// Implemented by the select awaitable. A winning case calls on_won.
struct notify : hce::awt_interface<int> {
    virtual ~notify() = default;
    virtual void on_won(int index) = 0;
};

namespace clause {

struct base {
    virtual ~base() = default;
    virtual bool activate(select::notify* owner) = 0;
    virtual void cancel() = 0;
    int index = -1;
};

} // clause

struct state {
    hce::spinlock lk;
    select::group* group = nullptr; // owned by the select object
    std::vector<std::unique_ptr<clause::base>>* clauses = nullptr;
    int winner = -1;
    bool parked = false;
    bool completed = false;
};

} // select

/// a request for a channel to do some operation
struct request {
    enum op_e {
        unknown,
        send_deep,
        send_shallow,
        try_send_deep,
        try_send_shallow,
        recv,
        try_recv
    };

    op_e op = op_e::unknown;
    void* value = nullptr; /// type erased value under operation
    select::group* group = nullptr;
    int index = -1;
    select::notify* notify = nullptr;
};

/// result of a channel try operation
enum result {
    closed = 0, /// channel is closed
    failure, /// channel operation failed
    success /// channel operation success
};

struct operation {
    /// a blocking operation, if constructed
    hce::awt<bool>::interface* awt = nullptr; 

    /// the immediate result, must be ignored in a blocking operation
    channel::result result = channel::result::closed; 
};

struct base : printable {
    virtual ~base(){}

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

    /// request an operation 
    virtual operation request(const request& r) = 0;

    struct selector {
        /*
         @brief build a select awaitable 

         The returned awaitable is not started. The select calls activate() to 
         register it under the channel lock.
         */
        virtual hce::awt<bool> request(const request& r) { return { }; }

        /**
         @brief activate a select awaitable
         */
        virtual bool activate(hce::awt<bool>::interface* op) { return false; }
    };

    virtual selector& select() = 0;
   
    /**
     @brief cancel a blocking send or recv operation
     */
    virtual void cancel(hce::awt<bool>::interface* op) { }
};

/**
 @brief interface for coroutine-safe channel implementations
 */
template <typename T>
struct interface : base {
    virtual ~interface(){}

    /// send a lvalue copy, awaitable returning true on success, else false
    hce::awt<bool> send(const T& t) {
        return { 
            request(channel::request{ 
                channel::request::op_e::send,
                channel::request::type_e::block,
                channel::request::copy_e::deep,
                (void*)&t
            }).awt 
        };
    }

    /// send a rvalue copy, awaitable returning true on success, else false
    hce::awt<bool> send(T&& t) {
        return { 
            request(channel::request{ 
                channel::request::op_e::send,
                channel::request::type_e::block,
                channel::request::copy_e::shallow,
                (void*)&t
            }).awt 
        };
    }

    /**
     @brief receive a value
     @return awaitable returning true on success, else false
     */
    hce::awt<bool> recv(T& t) {
        return { 
            request(channel::request{ 
                channel::request::op_e::recv,
                channel::request::type_e::block,
                channel::request::copy_e::deep,
                (void*)&t
            }).awt 
        };
    }

    /// attempt to send a lvalue copy
    hce::yield<channel::result> try_send(const T& t) {
        return { 
            request(channel::request{ 
                channel::request::op_e::send,
                channel::request::type_e::nonblock,
                channel::request::copy_e::deep,
                (void*)&t
            }).result
        };
    }

    /// attempt to send a rvalue copy
    hce::yield<channel::result> try_send(T&& t) {
        return { 
            request(channel::request{ 
                channel::request::op_e::send,
                channel::request::type_e::nonblock,
                channel::request::copy_e::shallow,
                (void*)&t
            }).result
        };
    }

    /**
     @brief attempt to receive a value
     @return awaitable returning true a result 
     */
    hce::yield<channel::result> try_recv(T& t) {
        return { 
            request(channel::request{ 
                channel::request::op_e::recv,
                channel::request::type_e::nonblock,
                channel::request::copy_e::deep,
                (void*)&t
            }).result
        };
    }
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

namespace send {

struct base : hce::awt_interface<bool> {
    base(transfer t, select::group* g, int idx, select::notify* own);
    bool try_claim();
    void on_resume(void* m) override;
    bool get_result();

    bool success = false;
    transfer tx;
    select::group* group = nullptr;
    int index = -1;
    select::notify* owner = nullptr;
};

template <typename LOCK>
struct interface : hce::awaitable::lockable<LOCK, base> {
    template <typename... As>
    interface(LOCK& lk, As&&... as) :
        hce::awaitable::lockable<LOCK, send::base>(
            lk,
            hce::awaitable::await::policy::defer,
            hce::awaitable::resume::policy::no_lock,
            std::forward<As>(as)...)
    { }
};

}

namespace recv {

struct base : hce::awt_interface<bool> {
    base(void* d, select::group* g, int idx, select::notify* own = nullptr);
    bool try_claim();
    void on_resume(void* m) override;
    bool get_result();

    bool success = false;
    void* destination = nullptr;
    select::group* group = nullptr;
    int index = -1;
    select::notify* owner = nullptr;
};

template <typename LOCK>
struct interface :
    hce::awaitable::lockable<LOCK, recv::base>
{
    template <typename... As>
    interface(LOCK& lk, As&&... as) :
          hce::awaitable::lockable<LOCK, recv::base>(
              lk,
              hce::awaitable::await::policy::defer,
              hce::awaitable::resume::policy::no_lock,
              std::forward<As>(as)...)
    { }
};

} // recv

namespace select {
namespace clause {

struct impl : clause::base {
    impl(channel::interface<T>& c, const channel::select::request& r, int idx);
    ~impl() = default;
    void cancel() override;

    hce::channel::base* ch;
    channel::select::request request;
    hce::awt<bool>::interface* iface = nullptr;
};

struct send : impl {
    template<typename... As>
    send(As&&... as) : impl(std::forward<As>(as)...) { }

    ~send() = default;
    bool activate(select::notify* owner) override;
};

struct recv : impl {
    template<typename... As>
    recv(As&&... as) : impl(std::forward<As>(as)...) { }

    ~recv() = default;
    bool activate(select::notify* owner) override;
};

} // clause
} // detail
} // select
} // channel 
} // hce
