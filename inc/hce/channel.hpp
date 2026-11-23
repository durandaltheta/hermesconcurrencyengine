#pragma once
//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
// c++
#include <typeinfo>
#include <cstddef>
#include <mutex>
#include <functional>
#include <utility>
#include <vector>
#include <atomic>
#include <memory>

// local
#include "hce/utility.hpp"
#include "hce/printable.hpp"
#include "hce/type.hpp"
#include "hce/memory.hpp"
#include "hce/atomic.hpp"
#include "hce/alloc.hpp"
#include "hce/circular_buffer.hpp"
#include "hce/coroutine.hpp"
#include "hce/scheduler.hpp"
#include "hce/channel/base.hpp"
#include "hce/channel/unbuffered.hpp"
#include "hce/channel/buffered.hpp"
#include "hce/channel/unlimited.hpp"

namespace hce {

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
        chan<T> ch;
        ch.construct<LOCK,ALLOCATOR>(std::forward<As>(as)...);
        return ch;
    }
       
    inline const std::type_info& type_info() const {
        return context_->type_info(); 
    }

    inline int size() const {
        return context_->size(); 
    }

    inline int used() const {
        return context_->used(); 
    }

    inline bool closed() const {
        return context_->closed(); 
    }

    inline void close() {
        return context_->close(); 
    }

    inline hce::awt<bool> send(T&& s) {
        return context_->send(std::move(s)); 
    }

    inline hce::awt<bool> send(const T& s) {
        return context_->send(s); 
    }

    inline hce::awt<bool> recv(T& r) {
        return context_->recv(r); 
    }

    inline hce::yield<channel::result> try_send(const T& s) {
        return context_->try_send(s);
    }

    inline hce::yield<channel::result> try_send(T&& s) {
        return context_->try_send(std::move(s));
    }

    inline hce::yield<channel::result> try_recv(T& r) {
        return context_->try_recv(r);
    }

    inline void set_guard(channel::guard* g) { 
        context_->set_guard(g);
    }

private:
    std::shared_ptr<channel::interface<T>> context_;
}; // hce::chan<T>


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
    // clause interface
    struct interface {
        typedef void(*deleter)(interface*);

        virtual ~interface();
        virtual channel::base_interface* get_channel_base_interface() = 0;
        virtual void launch() = 0; // launch forwarding operation
        virtual void finish() = 0; // end forwarding
       
        multiplex* parent;  // owning multiplex  
        size_t idx = 0;     // index of this clause
        hce::awt<void> awt; // launched coroutine awaitable
        callback call;      // registered callback
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

public:
    /**
     @brief an object which connects a value and channel to a callback
     */
    struct clause {
        /**
         The user must guarantee the argument value T stays in existence.

         @param t the value to assign over the channel. 
         @param ch channel to received from
         @param cb callback to execute when t is received
         */
        template <typename T>
        clause(T& t, hce::chan<T> ch, callback cb) :
            e(make_interface(t, std::move(ch), std::move(cb)), &deleter<T>)
        { }

    private:
        template <typename T>
        static interface* make_interface(T& t, hce::chan<T>&& ch, callback&& cb) {
            interface* i = hce::allocate<impl<T>>(1);
            ::new (static_cast<void*>(i)) impl<T>(t, std::move(ch));
            i->call = std::move(cb);
            return i;
        }
        
        template <typename T>
        static inline void deleter(interface* i) {
            if(i) {
                i->~interface();
                hce::deallocate(i); 
            }
        }

        erased e;
        friend struct multiplex;
    };


    /**
     @brief launch a multiplex of clauses  

     Start clause's operations to select between channels.

     Can be called more than once to multiplex additional channels.
     */
    template <typename... Es>
    multiplex(Es&&... es) {
        ready_ = hce::chan<state_t>::make();
        clauses_.reserve(clauses_.size() + sizeof...(es)); 
        std::vector<channel::base_interface*> bis;
        launch_(bis, std::forward<Es>(es)...);
    }

    /// WARNING: finish() must be called or this will throw a fatal
    ~multiplex();

    static std::string info_name();
    std::string name() const;

    /**
     @brief await until some callback gets called or multiplexing is finish()ed 

     No callbacks will execute once this method has returned, until it is called 
     again.

     @return await the result to get whether the multiplex succeeded
     */
    hce::awt<bool> select();

    /**
     @brief end multiplexing 

     This unregisters this object from associated clause channels.

     @return await the result to finish multiplexing operations
     */
    hce::awt<void> finish();

private:
    struct state_t {
        size_t idx = 0;
        bool done = false;
    };
    
    // clause implementation
    template <typename T>
    struct impl : interface {
        virtual ~impl() { }

        template <typename CHAN>
        impl(T& t, CHAN&& c) :
            value(t),
            ch(std::forward<CHAN>(c))
        { }

        inline channel::base_interface* get_channel_base_interface() {
            return ch.context().get();
        }

        inline void launch() { awt = hce::schedule(forwarder(this)); }

        inline void finish() { 
            // unregister for guarded receives with the channel
            self->ch.withdraw(parent.participant_);
        }

        // A single shot coroutine to receive over the multiplexed channel
        static inline hce::co<void> forwarder(impl<T>* self) {
            state_t result;
            result.idx = self->idx;
            auto& parent = *(self->parent);

            // register for guarded receives with the channel
            self->ch.participate(parent.participant_.get());

            // recv blocks until:
            // A) a send succeeds in acquiring the guard lock 
            // B) the parent finishes processing the callback and releases the claim
            while(co_await self->ch.recv(self->value, parent.participant_.get())) {
                // we have the guard claimed, notify the selector 
                co_await parent.ready_.send(result);
            } 

            // withdraw immediately
            self->ch.withdraw(parent.participant_.get());

            // notify clause finished
            result.done = true;
            co_await self->parent->ready_.send(result);
        }

        T& value;
        hce::chan<T> ch;
        hce::awt<void> awt;
    };

    hce::co<bool> selector_(hce::multiplex*);

    void launch_(std::vector<channel::base_interface*>&); 

    template <typename... Es>
    void launch_(std::vector<channel::base_interface*> bis, clause e, Es&&... es) {
        clauses_.push_back(std::move(e));
        auto& intf = *(clauses_.back().e.interface);
        intf.parent = this;
        intf.idx = clauses_.size() - 1;
        bis.push_back(intf.get_channel_base_interface());
        launch_(std::forward<Es>(es)...);
    }

    struct participant : channel::participant {
        // return whether operations are finished
        bool finished();

        // set operations as finished
        void finish(bool val);

    private:
        std::atomic<bool> finished_;
    };

    std::unique_ptr<participant> participant_;
    std::vector<clause> clauses_; 
    hce::chan<state_t> ready_;
}; // multiplex

}
