//SPDX-License-Identifier: Apache-2.0
//Author: Blayne Dennis 
#ifndef __HERMES_COROUTINE_ENGINE_CHANNEL__
#define __HERMES_COROUTINE_ENGINE_CHANNEL__

// c++
#include <typeinfo>
#include <deque>
#include <mutex>

// local
#include "atomic.hpp"
#include "circular_buffer.hpp"
#include "coroutine.hpp"
#include "scheduler.hpp"

namespace hce {

/** 
A communication object which can represent any object which implements 
channel<T>::interface interface.
*/
template <typename T>
struct channel {
    /// template type for channel
    typedef T value_type;

    /// result of a channel try operation
    enum result {
        closed = 0, /// channel is closed
        failure, /// channel operation failed
        success /// channel operation succeeded
    };

    /// interface for a channel implmentation
    struct interface {
        virtual ~implmentation(){}

        /// type_info of the implementation
        virtual const std::type_info& type_info() const = 0;

        /**
         @brief maximum capacity of stored value cache 

        Typically:
        >0 == cache of a specific maximum size
        0 == cache of no size (direct point to point data transfer)
        <0 == cache of unlimited size
         */
        virtual int capacity() const = 0;

        /// current size of the stored value cache
        virtual int size() const = 0;

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
        virtual hce::yield<result> try_send(const T& t) = 0;

        /// attempt to send a rvalue copy
        virtual hce::yield<result> try_send(T&& t) = 0;

        /// attempt receive a value
        virtual hce::yield<result> try_recv(T& t) = 0;
    };

    /**
     @brief construct a channel with an unbuffered implementation

     Specify a LOCK type of `hce::lockfree` to make the implementation lockfree 
     (only safe when all instances of the channel are used from the same system 
     thread). Lockfree channels are a very fast way to communicate between two 
     coroutines running on the same scheduler.
     */
    template <typename LOCK=hce::spinlock>
    inline channel<T>& construct() {
        context = std::make_shared<interface>(
            static_cast<interface*>(
                new unbuffered<LOCK>()));

        return *this;
    }

    /**
     @brief construct a channel with an buffered implementation
     */
    template <typename LOCK=hce::spinlock>
    inline channel<T>& construct(int sz) {
        context = std::make_shared<interface>(
            static_cast<interface*>(
                new buffered<LOCK>(sz)))

        return *this;
    }

    /**
     @brief construct a channel with a custom implementation
     */
    inline channel<T>& construct(std::shared_ptr<interface> i) {
        context = std::move(i);
        return *this;
    }

    template <typename LOCK=hce::spinlock>
    static inline channel make(int sz = 0) {
        return channel<T>().construct<LOCK>(sz);
    }

    channel() = default;
    channel(const channel<T>& rhs) = default;
    channel(channel<T>&& rhs) = default;

    channel<T>& operator=(const channel<T>& rhs) = default;
    channel<T>& operator=(channel<T>&& rhs) = default;
       
    /// retrieve the implementation's actual std::type_info
    inline const std::type_info& type_info() const { 
        return context->type_info(); 
    }

    /// return the maximum buffer size
    inline int capacity() const { return context->capacity(); }

    /// return the number of values in the buffer
    inline int size() const { return context->size(); }

    /// return if channel is closed
    inline bool closed() const { return context->closed(); }

    /// close channel
    inline void close() { return context->close(); }

    /// `co_await`able send operation 
    template <typename T2>
    inline hce::awt<bool> send(T2&& s) {
        return context->send(std::forward<T2>(s)); 
    }

    /// `co_await`able recv operation
    inline hce::awt<bool> recv(T& r) {
        return context->recv(r); 
    }

    /// `co_await`able try_send operation
    template <typename T2>
    inline hce::awt<bool> try_send(T2&& s) {
        return context->try_send(std::forward<T2>(s));
    }

    /// `co_await`able try_recv operation
    inline hce::yield<result> try_recv(T& r) {
        return context->try_recv(r);
    }

    /// return whether channel has a shared get pointer
    inline explicit operator bool() const { return (bool)(context.get()); }

    inline bool operator==(const channel<T>& rhs) const {
        return context.get() == rhs.context.get();
    }

    inline bool operator!=(const channel<T>& rhs) const {
        return !(*this == rhs);
    }

    inline bool operator<(const channel<T>& rhs) const {
        return context.get() < rhs.context.get();
    }

    inline bool operator<=(const channel<T>& rhs) const {
        return context.get() < rhs.context.get();
    }

    inline bool operator>(const channel<T>& rhs) const {
        return context.get() > rhs.context.get();
    }

    inline bool operator>=(const channel<T>& rhs) const {
        return context.get() > rhs.context.get();
    }

    template <typename LOCK>
    struct unbuffered : public interface {
        unbuffered() : closed_flag_(false) {}
        
        virtual ~unbuffered(){}

        inline const std::type_info& type_info() const {
            return typeid(unbuffered); 
        }

        inline int capacity() const { return 0; }
        inline int size() const { return 0; }

        inline bool closed() const {
            std::unique_lock<LOCK> lk(lk_);
            return closed_flag_;
        }

        inline void close() {
            std::unique_lock<LOCK> lk(lk_);
            if(!closed_flag_) {
                closed_flag_ = true;
                for(auto& p : parked_send_) { p->resume(nullptr); }
                for(auto& p : parked_recv_) { p->resume(nullptr); }
                parked_send_.clear();
                parked_recv_.clear()
            }
        }

        inline awt<bool> send(const T& t) {
            return send_((void*)t, false);
        }

        inline awt<bool> send(T&& t) {
            return send_((void*)t, true);
        }

        inline awt<bool> recv(T& t) {
            return recv_((void*)t);
        }

        inline yield<result> try_send(const T& t) {
            return try_send_((void*)t, false);
        }

        inline yield<result> try_send(T&& t) {
            return try_send_((void*)t, true);
        }

        inline yield<result> try_recv(T& t) {
            return recv_((void*)t);
        }

    private:
        struct send_pair {
            send_pair() : source_(nullptr), is_rvalue_(false) { }

            send_pair(void* source, bool is_rvalue) : 
                source_(source), 
                is_rvalue_(is_rvalue) 
            { }

            inline void send(void* destination) {
                if(is_rvalue_) {
                    *((T*)destination) = std::move(*((T*)(source_))); 
                } else {
                    *((T*)destination) = *((const T*)(source_)); 
                }
            }

        private:
            void* source_;
            bool is_rvalue_;
        };

        inline hce::awt<bool> send_(void* s, bool is_rvalue) {
            struct send_interface : 
                public hce::scheduler::reschedule<
                    hce::awaitable::lockable::lockfree<
                        hce::awt<bool>::interface 
                    >
                >
            {
                send_interface(bool ready, bool success, send_pair sp) :
                    ready_(ready),
                    success_(success),
                    sp_(sp)
                { }

                inline bool ready_interface() { return ready_; }

                inline void resume_interface(void* m) {
                    if(m) {
                        sp_.send(m);
                        success_ = true;
                    }
                    else{ success_ = false; }
                }

                inline bool result_interface() { return success_; }

            private:
                bool ready_;
                bool success_;
                send_pair sp_;
            };

            std::unique_lock<LOCK> lk(lk_);

            if(closed_flag_) {
                return hce::awt<bool>(new send_interface(true, false, send_pair()));
            }

            if(parked_recv_.size()) {
                send_pair sp{s,false};
                parked_recv_.front()->resume((void*)(&sp));
                parked_recv_.pop_front();

                // return an awaitable which immediately returns true
                return hce::awt<bool>(new send_interface(true, true, send_pair()));
            } else {
                auto ai = new send_interface(std::move(lk), false, true, send_pair(s, is_rvalue ));
                parked_send_.push_back(ai);
                return hce::awt<bool>(ai);
            }
        }

        /// stackless `co_await`able recv operation
        inline hce::awt<bool> recv_(void* r) {
            struct recv_interface : 
                public hce::scheduler::reschedule<
                    hce::awaitable::lockable::lockfree<
                        hce::awt<bool>::interface 
                    >
                >
            {
                send_interface(bool ready, bool success, void* r) :
                    ready_(ready),
                    success_(success),
                    r_(r)
                { }

                inline bool ready_interface() { return ready; }

                inline void resume_interface(void* m) {
                    if(m) {
                        ((send_pair*)m)->send(r_);
                        success_ = true;
                    }
                    else { success_ = false; }
                }

                inline bool result_interface() { return success_; }

                bool ready_;
                bool success_;
                void* r_; // pointer to receiver memory
            };

            std::unique_lock<LOCK> lk(lk_);

            if(closed_flag_) { 
                return hce::awt<bool>(new recv_interface(true, false, nullptr));
            } else if(parked_send_.size()) {
                parked_send_.front()->resume(r);

                // return an awaitable which immediately returns true
                return hce::awt<bool>(new recv_interface(true, true, nullptr));
            } else {
                auto ai = new recv_interface(false, true, r);
                parked_send_.push_back(ai);
                return hce::awt<bool>(ai);
            }
        }
        
        inline hce::yield<result> try_send_(void* s, bool is_rvalue) {
            std::unique_lock<LOCK> lk(lk_);

            if(closed_flag_) { return { base_channel::closed }; }
            else if(parked_recv_.size()) {
                send_pair sp{s,is_rvalue};
                parked_recv_.front()->resume((void*)(&sp));
                parked_recv_.pop_front();

                // return an awaitable which immediately returns true
                return { base_channel::success }; 
            }
            else { return { base_channel::failure }; }
        }
        
        inline hce::yield<result> try_recv_(void* r) {
            std::unique_lock<LOCK> lk(lk_);

            if(closed_flag_) { return { base_channel::closed }; }
            else if(parked_send_.size()) {
                parked_send_.front()->resume(r);

                // return an awaitable which immediately returns true
                return { base_channel::success }; 
            }
            else { return { base_channel::failure }; }
        }

        mutable LOCK lk_;
        bool closed_flag_;
        std::deque<awt<bool>::interface*> parked_send_;
        std::deque<awt<bool>::interface*> parked_recv_;
    };

    template <typename LOCK>
    struct buffered : public interface {
        virtual ~buffered(){}

        buffered(int sz) : buf_(sz < 0 ? (size_t)1 : (size_t)sz) { }

        inline const std::type_info& type_info() const {
            return typeid(buffered); 
        }

        inline int capacity() const {
            std::lock_guard<LOCK> lk(lk_);
            return (int)buf_.capacity();
        }

        inline size_t size() const {
            std::lock_guard<LOCK> lk(lk_);
            return (int)buf_.size();
        }

        inline bool closed() const {
            std::lock_guard<LOCK> lk(lk_);
            return closed_flag_;
        }

        inline void close() {
            std::unique_lock<LOCK> lk(lk_);
            if(!closed_flag_) {
                closed_flag_ = true;
                for(auto& p : parked_send_) { p->resume(nullptr); }
                for(auto& p : parked_recv_) { p->resume(nullptr); }
                parked_send_.clear();
                parked_recv_.clear()
            }
        }

        inline awt<bool> send(const T& t) {
            return send_((void*)t, false);
        }

        inline awt<bool> send(T&& t) {
            return send_((void*)t, true);
        }

        inline awt<bool> recv(T& t) {
            return recv_((void*)t);
        }

        inline yield<result> try_send(const T& t) {
            return try_send_((void*)t, false);
        }

        inline yield<result> try_send(T&& t) {
            return try_send_((void*)t, true);
        }

        inline yield<result> try_recv(T& t) {
            return recv_((void*)t);
        }

    private:
        struct send_pair {
            inline void send(void* destination) {
                auto& d = *((circular_buffer<T>*)destination);

                if(is_rvalue) {
                    d.push(std::move(*((T*)(sp.source)))); 
                } else {
                    d.push(*((const T*)(sp.source))); 
                }
            }

            void* source;
            bool is_rvalue;
        };

        inline hce::awt<bool> send_(void* s, bool is_rvalue) {
            struct send_interface : 
                public hce::scheduler::reschedule<
                    hce::awaitable::lockable::lockfree<
                        hce::awt<bool>::interface 
                    >
                >
            {
                send_interface(bool ready, bool success, send_pair sp) :
                    ready_(ready),
                    success_(success),
                    sp_(sp)
                { }

                inline bool ready() { return ready_; }

                inline void resume(void* m) {
                    if(m) {
                        sp_.send(m);
                        success_ = true;
                    }
                    else { success_ = false; }
                }

                inline bool result() { return success_; }

                bool ready_;
                bool success_;
                send_pair sp_; 
            };

            std::unique_lock<LOCK> lk(lk_);

            if(closed_flag_) {
                return hce::awt<bool>(new send_interface(true, false));
            } else if(buf_.full()) {
                auto ai = new send_interface(false, true, std::move(lk), { s, is_rvalue});
                parked_send_.push_back();
                return hce::awt<bool>(ai);
            } else {
                send_pair sp{s,is_rvalue};
                sp.send((void*)&buf_);

                if(parked_recv_.size()) {
                    parked_recv_.front()((void*)&buf_);
                    parked_recv_.pop_front();
                }

                // return an awaitable which immediately returns true
                return hce::awt<bool>(new send_interface(true, true));
            }
        }

        /// stackless `co_await`able recv operation
        inline hce::awt<bool> recv_(void* r) {
            struct recv_interface : 
                public hce::scheduler::reschedule<
                    hce::awaitable::lockable::lockfree<
                        hce::awt<bool>::interface 
                    >
                >
            {
                recv_interface(bool ready, bool success, void* r) :
                    ready_(ready),
                    success_(success),
                    r_(r)
                { }

                inline bool ready() { return ready_; }

                inline void resume(void* m) {
                    if(m) {
                        auto b = (circular_buffer<T>*)m;
                        *((T*)r_) = std::move(b->front());
                        b->pop();
                        success_ = true;
                    }
                    else { success_ = false; }
                }

                inline bool result() { return success; }

                bool ready_;
                bool success_;
                void* r_; // pointer to receiver memory
            };

            std::unique_lock<LOCK> lk(lk_);

            if(closed_flag_) {
                return hce::awt<bool>(new recv_interface(true, false, nullptr));
            } else if(buf_.empty()) {
                auto ai = new recv_interface(false, true, r);
                parked_send_.push_back(ai);
                return hce::awt<bool>(ai);
            } else {
                *((T*)r) = std::move(buf_.front());
                buf_.pop();

                if(parked_send_.size()) {
                    parked_send_.front()((void*)&buf_);
                    parked_send_.pop_front();
                }

                // return an awaitable which immediately returns true
                return hce::awt<bool>(new recv_interface(true, true, nullptr));
            }
        }
        
        inline hce::yield<result> try_send_(void* s, bool is_rvalue) {
            std::unique_lock<LOCK> lk(lk_);

            if(closed_flag_) { return { base_channel::closed }; }
            else if(!buf_.full()) {
                send_pair sp(s,is_rvalue);
                sp.send((void*)&buf_);

                if(parked_recv_.size()) {
                    parked_recv_.front()((void*)&buf_);
                    parked_recv_.pop_front();
                }

                // return an awaitable which immediately returns true
                return { base_channel::success }; 
            }
            else { return { base_channel::failure }; }
        }
        
        inline hce::yield<result> try_recv_(void* r) {
            std::unique_lock<LOCK> lk(lk_);

            if(closed_flag_) { return { base_channel::closed }; }
            else if(buf_.size()) {
                *((T*)r) = std::move(buf_.front());
                buf_.pop_front();

                if(parked_send_.size()) {
                    parked_send_.front()((void*)&buf_);
                    parked_send_.pop_front();
                }

                // return an awaitable which immediately returns true
                return { base_channel::success }; 
            }
            else { return { base_channel::failure }; }
        }

        mutable LOCK lk_;
        bool closed_flag_;
        hce::circular_buffer<T> buf_;
        std::deque<awt<bool>::interface*> parked_send_;
        std::deque<awt<bool>::interface*> parked_recv_;
    };

private:
    std::shared_ptr<interface> context;
};

}

#endif
