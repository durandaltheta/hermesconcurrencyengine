#pragma once
//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
// c++
#include <utility>
#include <atomic>
#include <set>
#include <vector>

// local
#include "hce/utility.hpp"
#include "hce/atomic.hpp"
#include "hce/printable.hpp"
#include "hce/list.hpp"
#include "hce/circular_buffer.hpp"
#include "hce/scheduler.hpp"

namespace hce {
namespace channel {
namespace detail {

struct sync_t;

// an atomic to guard claims for receive operations
struct guard_t {
    // notify callback for when release() is called
    struct notify_t {
        typedef void (*on_release_t)(void*);

        void* ctx = nullptr;
        on_release_t on_release = nullptr;
    };

    guard_t(notify_t n) : notify_(n) { }

    /*
     Called by the interface *every* time a recv occurs when there's 
     participants competing for guarded receives. Returns true if the recv 
     can proceed, and false if the channel must block.

     Accepts the ctx context pointer.
     */
    inline bool try_claim_recv() {
        bool expected = false;
        return claimed_.compare_exchange_strong(
            expected, true,
            std::memory_order_acq_rel,
            std::memory_order_acquire);
    }

    inline bool claimed() {
        return claimed_.load(std::memory_order_acquire);
    }

    // release the guard
    inline void release() { 
        release_();
        notify_.on_release(notify_.ctx);
    }

private:
    inline void release_() {
        claimed_.store(false, std::memory_order_release); 
    }

    const notify_t notify_;
    std::atomic<bool> claimed_{false};
    friend struct sync_t;
};

} // detail

/// the most basic channel interface
struct base_interface : printable {
    /*
     Indicate to the channel that a participant wants to participate in 
     synchronized receiving. Must be called prior to passing the participant 
     pointer to receive operations.

     This operation will insert the channel's guard_t into the participant.
     */
    virtual void participate(participant* p) = 0;

    /*
     Indicate to the channel the participant is withdrawing from synchronized 
     receiving.

     The channel must cancel and resume all blocked operations associated 
     with the participant.

     This operation will erase the channel's guard_t from the participant.
     */
    virtual void withdraw(participant* p) = 0;
};

/**
 @brief a participant in guarded receiving 

 Guarded receives allow a participant to attempt to receive over multiple 
 channels at once. 

 Any successful receive operations will be a synchronized among *all* channels 
 where the participant called participate().

 The caller of the receive operation must call release() to allow other 
 receives to succeed. 

 When the participant wants to end guarded receives, it must call withdraw().

 Once receives begin it is *NOT* safe to call participate() with this object
 again. Instead, the owner *MUST* call withdraw() on *ALL* channels first.
 */
struct participant {
    participant() = delete;
    participant(const participant&) = delete;
    participant(participant&) = delete;

    participant(std::vector<base_interface*> cs) : 
            channels_(std::move(cs)) 
    { 
        for(const auto channel : channels_) {
            channel->participate(*this);
        }

        // the structure of guards_ is constant post construction
        current_ = syncs_.begin();
    }

    ~participant() {
        for(const auto channel : channels_) {
            channel->withdraw(*this);
        }
    }

    participant& operator=(const participant&) = delete;
    participant& operator=(participant&) = delete;

    // release all the guards
    void release();
   
private:
    // insert a guard
    void insert(detail::guard_t*);

    // remove a guard
    void erase(detail::guard_t*);

    // attempt to claim all the guards
    bool try_claim_recv();

    // find a new valid current_
    void next();

    std::vector<base_interface*> channels_;

    /// only used in insert and erase during construction and destruction
    std::mutex mtx_;

    // ordered set of channel guards
    std::set<detail::guard_t*> guards_;

    // used for round robin iteration
    std::set<detail::guard_t*>::iterator current_;

    friend struct sync_t;
};

/// the multi-participant synchronization data held by a channel instance
struct sync_t {
    sync_t(guard_t g) : guard(g) { }

    inline void insert(participant* p) { 
        participants.push_back(p);
        p->insert(&guard); 
    }

    inline void erase(participant* p) { 
        p->erase(&guard); 
        sync.participants.remove_if([&](participant* cur) { return p == cur; });
    }

    /// return true if the sync is in active use
    inline bool active() { return participants.size(); }

    /// return true if the sync is claimed
    inline bool claimed() { return guard.claimed(); }

    /// try to claim all guards known by the participant, else claim the guard_t
    inline bool try_claim_recv(participant* p) { 
        return p ? p->try_claim_recv() : guard.try_claim_recv();
    }

    /// try to release all guards known by the participant, else release the guard_t
    inline void release(participant* p) { 
        return p ? p->release() : guard.release();
    }

    guard_t guard;
    hce::list<participant*> participants;
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
struct interface : base_interface {
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

    /**
     @brief receive a value
     @param p context indicating *who* is participating in the guarded receive
     @return awaitable returning true on success, else false
     */
    virtual hce::awt<bool> recv(T& t, participant* p) = 0;

    /// attempt to send a lvalue copy
    virtual hce::yield<channel::result> try_send(const T& t) = 0;

    /// attempt to send a rvalue copy
    virtual hce::yield<channel::result> try_send(T&& t) = 0;

    /**
     @brief attempt to receive a value
     @return awaitable returning true a result 
     */
    virtual hce::yield<channel::result> try_recv(T& t) = 0;

    /**
     @brief attempt to receive a value
     @param p context indicating *who* is participating in the guarded receive
     @return awaitable returning true a result 
     */
    virtual hce::yield<channel::result> try_recv(T& t, participant* p) = 0;
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
