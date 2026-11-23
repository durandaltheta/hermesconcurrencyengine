#pragma once
//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
// c++
#include <utility>
#include <atomic>
#include <vector>
#include <type_traits>

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
namespace select {

/// group used for managing selects
struct group {
    std::atomic<bool> claimed{false};
    std::atomic<int>  winner{-1};

    bool try_claim(int index) {
        bool expected = false;
        if(claimed.compare_exchange_strong(
                expected, true,
                std::memory_order_acq_rel,
                std::memory_order_acquire)) {
            winner.store(index, std::memory_order_release);
            return true;
        }

        return false;
    }

    bool is_claimed() const {
        return claimed.load(std::memory_order_acquire);
    }

    int winner_index() const {
        return winner.load(std::memory_order_acquire);
    }
};

// Implemented by the select awaitable. A winning case calls on_won.
struct notify {
    virtual ~notify() = default;
    virtual void on_won(int index) = 0;
};

struct request {
    enum struct type_e {
        copy,
        move
    };

    type_e type = type_e::unknown;
    void* value = nullptr;
    select::group* group = nullptr;
    int index = -1;
    select::notify* notify = nullptr;
};

struct case_base {
    virtual ~case_base() = default;
    virtual bool activate(select::notify* owner) = 0;
    virtual void cancel() = 0;
    virtual int index() const = 0;
};

struct state {
    hce::spinlock lk;
    select::group* group = nullptr; // owned by the select object
    std::vector<std::unique_ptr<case_base>> cases;
    int winner = -1;
    bool parked = false;
    bool completed = false;
};

} // select

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

    struct selector {
        /*
         @brief build a select-send awaitable 

         The returned awaitbale is not started. The select calls activate() to 
         register it under the channel lock.
         */
        virtual hce::awt<bool> send(const request& r) { return { }; }

        /*
         @brief build a select-recv awaitable 

         The returned awaitbale is not started. The select calls activate() to 
         register it under the channel lock.
         */
        virtual hce::awt<bool> recv(const request& r) { return { }; }

        /**
         @brief activate a select awaitable
         */
        virtual bool activate(hce::awt<bool>::interface* op) { return false; }
    };

    virtual selector& select() = 0;
   
    /**
     @brief cancel a send or recv operation
     */
    virtual void cancel(hce::awt<bool>::interface* op) { }
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
    hce::awaitable::lockable<LOCK, hce::awt_interface<bool>>
{
    base_send_interface(LOCK& lk, transfer t, select::group* g, int idx,
                        select::notify* own = nullptr) :
          hce::awaitable::lockable<LOCK, hce::awt_interface<bool>>(
              lk,
              hce::awaitable::await::policy::defer,
              hce::awaitable::resume::policy::no_lock),
        tx(t),
        group(g),
        index(idx),
        owner(own)
    {}

    bool try_claim() {
        return group ? group->try_claim(index) : true;
    }

    inline void on_resume(void* m) override {
        if(m) {
            tx.send(static_cast<void*>(m));
            success = true;
            if(owner){ owner->on_won(index); }
        }
    }

    inline bool get_result() { return success; }

    bool success = false;
    transfer tx;
    select::group* group = nullptr;
    int index = -1;
    select::notify* owner = nullptr;
};

template <typename LOCK>
struct base_recv_interface :
    hce::awaitable::lockable<LOCK, hce::awt_interface<bool>>
{
    base_recv_interface(LOCK& lk, void* d, select::group* g, int idx,
                        select::notify* own = nullptr) :
          hce::awaitable::lockable<LOCK, hce::awt_interface<bool>>(
              lk,
              hce::awaitable::await::policy::defer,
              hce::awaitable::resume::policy::no_lock),
        destination(d),
        group(g),
        index(idx),
        owner(own)
    {}

    bool try_claim() {
        return group ? group->try_claim(index) : true;
    }

    inline void on_resume(void* m) override {
        if(m) {
            static_cast<transfer*>(m)->send(destination);
            success = true;
            if(owner){ owner->on_won(index); }
        }
    }

    inline bool get_result() { return success; }

    bool success = false;
    void* destination = nullptr;
    select::group* group = nullptr;
    int index = -1;
    select::notify* owner = nullptr;
};

namespace select {

struct case_impl : case_base {
    channel::select::request request;
    hce::awt<bool> op;
    hce::awt<bool>::interface* iface = nullptr;

    case_impl(channel::interface<T>& c, const channel::select::request r&)
        : ch(&c), request(r) {}

    void cancel() override {
        if(iface && ch){ 
            ch->cancel(iface); 
        }
    }

    int index() const override { return idx; }
};

struct case_send : case_impl {
    template<typename... As>
    case_send(As&&... as) : case_impl(std::forward<As>(as)...) { }

    bool activate(select::notify* owner) override {
        request.owner = owner;
        op = ch->select().send(request);
        iface = dynamic_cast<hce::awt<bool>::interface*>(&op.implementation());
        return ch->select().activate(iface);
    }
};

struct case_recv : case_impl {
    template<typename... As>
    case_recv(As&&... as) : case_impl(std::forward<As>(as)...) { }

    bool activate(select::notify* owner) override {
        request.owner = owner;
        op = ch->select().recv(request);
        iface = dynamic_cast<hce::awt<bool>::interface*>(&op.implementation());
        return ch->select().activate(iface);
    }
};

struct impl : 
    hce::awaitable::lockable<hce::spinlock, hce::awt_interface<int>>, 
    select::notify
{
    std::unique_ptr<state> st;

    explicit impl(std::unique_ptr<state> s):
          hce::awaitable::lockable<hce::spinlock, hce::awt_interface<int>>(
              s->lk,
              hce::awaitable::await::policy::defer,
              hce::awaitable::resume::policy::no_lock)
        , st(std::move(s))
    {}

    static inline std::string info_name() { return "hce::channel::detail::select::impl"; }
    inline std::string name() const { return impl::info_name(); }

    bool on_ready() override {
        int parked_count = 0;

        for(auto& c : st->cases) {
            if(c->activate(this)) { ++parked_count; }
        }

        st->parked = true;

        if(st->group->is_claimed()) {
            st->winner = st->group->winner_index();
            cancel_losers();
            st->completed = true;
            return true;
        }

        if(parked_count == 0) {
            st->winner = -1;
            st->completed = true;
            return true;
        }

        return false;
    }

    void on_resume(void*) override {
        if(st->winner < 0) { st->winner = st->group->winner_index(); }
        cancel_losers();
        st->completed = true;
    }

    int get_result() override { return st->winner; }

    void on_won(int index) override {
        st->winner = index;
        if(st->parked && !st->completed) {
            st->completed = true;
            this->resume(nullptr);
        }
    }

private:
    void cancel_losers() {
        int w = st->winner;

        for(auto& c : st->cases) {
            if(c->index() == w) { continue; }
            c->cancel();
        }
    }
};

} // detail
} // select
} // channel 

/**
 @brief register with multiple channels for send or receive

 Example:
 ```
 hce::co<bool> foo(hce::channel::unbuffered<int>* ich, 
                   hce::channel::unbuffered<std::string>* sch) {
     hce::select select;
     int i;
     std::string s = "hello";
     bool done = false

     while(!done) {
         // build our channel requests
         select.recv(*ich, i, [&]{ 
             std::cout << "received int: " << i << std::endl;
         });

         select.send(*sch, s, [&]{ 
             done = true; // we can access outside context
             std::cout << "sent std::string: " << s << std::endl;
         });

         if(!select.call(co_await select.wait())) {
            // we were all canceled
            co_return false;
         }
     }
            
     co_return true;
 }
 ```
 */
struct select {
    template <typename T, typename CALLBACK>
    select& send(channel::interface<T>& ch, T&& src, CALLBACK&& cb) {
        channel::select::request::type_e type;

        if constexpr (std::is_rvalue_reference_v<decltype(src)>) {
            type = channel::select::request::type_e::move;
        } else {
            type = channel::select::request::type_e::copy;
        }

        int idx = static_cast<int>(cases.size());
        channel::select::request r{ type, &dest, group, idx };
        return append( 
            std::make_unique<channel::detail::send_case<T>>(ch, r),
            callback_impl<CALLBACK>::make(std::forward<CALLBACK>(cb))
        );
    }

    template <typename T, typename CALLBACK>
    select& recv(channel::interface<T>& ch, T& dest, CALLBACK&& cb) {
        int idx = static_cast<int>(cases.size());
        channel::select::request r{ 
            channel::select::request::type_e::copy, &dest, group, idx 
        };
        return append( 
            std::make_unique<channel::detail::case_recv>(ch, r),
            callback_impl<CALLBACK>::make(std::forward<CALLBACK>(cb))
        );
    }

    /*
     returns an awaitable that parks until exactly one case wins.

     The select object must remain alive until the awaitable completes.
     Result is the winning case index, or -1 if every case failed.
     */
    inline hce::awt<int> wait() {
        auto state = std::make_unique<state>();
        state->group = &group;
        state->cases = std::move(cases);
        return hce::awt<int>(new channel::select::impl(std::move(state)));
    }

    /**
     @brief call the selected callback 
     
     Callbacks are cleared after every call().
     */
    inline bool call(int idx) { 
        bool result = false;

        if(idx > -1 && callbacks[idx]) {
            (*(callbacks[idx]))(); 
            result = true;
        } 

        callbacks.clear();
        return result;
    }

private:
    inline int append(
            std::unique_ptr<case_base>&& ca, 
            std::unique_ptr<callback> cb) 
    {
        cases.push_back(std::move(ca));
        callbacks.push_back(std::move(cb));
        return *this;
    }

    struct callback {
        virtual ~callback(){}
        virtual void call() = 0;
    };

    template <typename CALLBACK>
    struct callback_impl : callback {
        static callback_impl<CALLBACK>* make(const CALLBACK& c) { 
            return new callback_impl<CALLBACK>( c );
        }

        static callback_impl<CALLBACK>* make(CALLBACK&& c) { 
            return new callback_impl<CALLBACK>( std::move(c) );
        }

        ~callback_impl(){}
        inline void call() { cb(); }

        CALLBACK cb;
    };

    channel::select::group group;
    std::vector<std::unique_ptr<case_base>> cases;
    std::vector<std::unique_ptr<callback>> callbacks;
};
} // hce
