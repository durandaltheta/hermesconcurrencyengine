//SPDX-License-Identifier: Apache-2.0
//Author: Blayne Dennis 
#ifndef __HCE_COROUTINE_ENGINE_TEST_MODULE_HELPERS__
#define __HCE_COROUTINE_ENGINE_TEST_MODULE_HELPERS__

#include <mutex>
#include "hce.hpp"
#include "test_helpers.hpp"

namespace test {
namespace module {

template <unsigned int EXPECTED_SUCCESS_COUNT>
struct result_interface : public hce::printable {
    virtual ~result_interface() {
        if(!this->validated_) {
            HCE_FATAL_METHOD_BODY("~result_interface","result_interface did not have validate_results() called before being destroyed, cannot continue");
            std::terminate();
        }
    }

    inline bool handle_result(bool b) {
        std::lock_guard<std::mutex> lk(this->mtx_);

        if(b) {
            ++this->success_count_;
        } else {
            ++this->error_count_;
        }

        return b;
    }

    inline bool validate_results() {
        std::lock_guard<std::mutex> lk(this->mtx_);

        if(!this->validated_) {
            this->validated_ = true;

            if(this->error_count_) {
                HCE_ERROR_METHOD_BODY("validate","expected 0 errors, but ",this->error_count_," errors detected");
            } else if(this->success_count_ != EXPECTED_SUCCESS_COUNT){
                HCE_ERROR_METHOD_BODY("validate","expected ",EXPECTED_SUCCESS_COUNT," successes, but ",this->success_count_," successes recorded");
            } else {
                this->valid_ = true;
            }
        }

        return this->valid_;
    }

private:
    std::mutex mtx_;
    bool valid_ = false;
    bool validated_ = false;
    size_t success_count_ = 0;
    size_t error_count_ = 0;
};

template <bool PARALLEL>
struct scheduler {
    // implemented in child variants
    static inline hce::awt<bool> schedule(hce::co<bool>) {
        HCE_FATAL_FUNCTION_BODY("test::module::scheduler<PARALLEL>::schedule","cannot call default implementation");
        std::terminate();
        return hce::awt<bool>();
    }
};

template <>
struct scheduler<true> {
    static inline hce::awt<bool> schedule(hce::co<bool> co) {
        // scheduled in parallel
        return hce::threadpool::schedule(std::move(co));
    }
};

template <>
struct scheduler<false> {
    static inline hce::awt<bool> schedule(hce::co<bool> co) {
        // scheduled concurrently
        return hce::schedule(std::move(co));
    }
};

/*
 Implenents predictable channel sends and receives for a variety of 
 templated types and channel implementations, able to schedule coroutines 
 either concurrently or in parallel.

 Each usage on this object and on each child object is expected to have 
 send() and receive() called once before some test calls validate_results().
 */
template <bool PARALLEL>
struct channels : public result_interface<44> {
    channels() { }
    virtual ~channels(){}

    template <typename T>
    struct variants : public result_interface<16> {
        struct pair : public result_interface<8> {
            pair(int i) : 
                in(hce::chan<T>::make(i)),
                out(hce::chan<T>::make(i)),
                chan_type_(i == 0 
                    ? "unbuffered"
                    : i > 0
                        ? "buffered"
                        : "unlimited")
            { }

            virtual ~pair(){}

            static inline std::string info_name() { 
                return variants<T>::info_name() + "::pair";
            }

            inline std::string name() const { return pair::info_name(); }
            inline std::string content() const { return chan_type_; }

            hce::chan<T> in;
            hce::chan<T> out;

            inline hce::awt<bool> send(T t) {
                return scheduler<PARALLEL>::schedule(send_op(this, t));
            }

            inline hce::awt<bool> receive(T t) {
                return scheduler<PARALLEL>::schedule(receive_op(this, t));
            }

        private:
            const char* chan_type_;

            static inline hce::co<bool> send_op(variants<T>::pair* pr, T t) {
                std::string fname = pr->name() + "::send_op";
                bool success = false;
                success = pr->handle_result(co_await pr->in.send(t));

                if(success) {
                    T t2;
                    success = pr->handle_result(co_await pr->out.recv(t2));

                    if(success) {
                        success = t == t2;

                        if(!success) {
                            HCE_ERROR_FUNCTION_BODY(fname, "value mismatch, t[", t, "] != t2[", t2, "]");
                        }
                    }
                }

                co_return success;
            }

            static inline hce::co<bool> receive_op(variants<T>::pair* pr, T t) {
                std::string fname = pr->name() + "::receive_op";
                bool success = false;
                T t2;
                success = pr->handle_result(co_await pr->in.recv(t2));

                if(success) {
                    if(t == t2) {
                        success = pr->handle_result(co_await pr->out.send(t2));
                    } else {
                        HCE_ERROR_FUNCTION_BODY(fname, "value mismatch, t[", t, "] != t2[", t2, "]");
                        success = false;
                        pr->handle_result(co_await pr->out.send(t2));
                    }
                }

                co_return success;
            }
        };

        static inline std::string info_name() { 
            return channels<PARALLEL>::info_name() + hce::type::templatize<T>("::variants");
        }

        inline std::string name() const { return variants<T>::info_name(); }

        variants() :
            unbuf(0),
            buf(1),
            unlim(-1)
        { }
    
        virtual ~variants(){}

        pair unbuf;
        pair buf;
        pair unlim;

        inline hce::awt<bool> send(T t) {
            return scheduler<PARALLEL>::schedule(send_op(this, t));
        }

        inline hce::awt<bool> receive(T t) {
            return scheduler<PARALLEL>::schedule(receive_op(this, t));
        }

        // override so validate gets called internally
        inline bool validate_results() {
            return 
                this->result_interface<16>::validate_results() &&
                unbuf.validate_results() && 
                buf.validate_results() &&
                unlim.validate_results();
        }

    private:
        static inline hce::co<bool> send_op(variants<T>* vars, T t) {
            auto awt_unbuf = vars->unbuf.send(t);
            auto awt_buf = vars->buf.send(t);
            auto awt_unlim = vars->unlim.send(t);
            co_return vars->handle_result(
                vars->handle_result(co_await awt_unbuf) && 
                vars->handle_result(co_await awt_buf) && 
                vars->handle_result(co_await awt_unlim));
        }

        static inline hce::co<bool> receive_op(variants<T>* vars, T t) {
            auto awt_unbuf = vars->unbuf.receive(t);
            auto awt_buf = vars->buf.receive(t);
            auto awt_unlim = vars->unlim.receive(t);
            co_return vars->handle_result(
                vars->handle_result(co_await awt_unbuf) && 
                vars->handle_result(co_await awt_buf) && 
                vars->handle_result(co_await awt_unlim));
        }
    };

    variants<int> vars_int;
    variants<unsigned int> vars_unsigned_int;
    variants<size_t> vars_size_t;
    variants<float> vars_float;
    variants<double> vars_double;
    variants<char> vars_char;
    variants<void*> vars_voidp;
    variants<std::string> vars_std_string;
    variants<test::CustomObject> vars_CustomObject;

    static inline std::string info_name() { 
        return PARALLEL ? "test::module::channels<true>" 
                        : "test::module::channels<false>";
    }

    inline hce::awt<bool> send() {
        return scheduler<PARALLEL>::schedule(send_loop_op(this));
    }

    inline hce::awt<bool> receive() {
        return scheduler<PARALLEL>::schedule(receive_loop_op(this));
    }

    // override so validate gets called internally
    inline bool validate_results() {
        return 
            this->result_interface<44>::validate_results() &&
            vars_int.validate_results() &&
            vars_unsigned_int.validate_results() &&
            vars_size_t.validate_results() &&
            vars_float.validate_results() &&
            vars_double.validate_results() &&
            vars_char.validate_results() &&
            vars_voidp.validate_results() &&
            vars_std_string.validate_results() &&
            vars_CustomObject.validate_results();
    }

private:
    // expects 1100 successes
    static inline hce::co<bool> send_loop_op(channels* chs) {
        bool success=true;

        for(size_t i=0; success && i<100; ++i) {
            success = 
                chs->handle_result(
                    co_await scheduler<PARALLEL>::schedule(
                        send_op(chs, i)));
        }

        co_return success;
    }

    static inline hce::co<bool> send_op(channels* chs, size_t i) {
        co_return chs->handle_result(
            chs->handle_result(co_await chs->vars_int.send((int)test::init<int>(i))));
            //chs->handle_result(co_await chs->vars_int.send((int)test::init<int>(i))) && 
            //chs->handle_result(co_await chs->vars_unsigned_int.send((unsigned int)test::init<unsigned int>(i))) && 
            //chs->handle_result(co_await chs->vars_size_t.send((size_t)test::init<size_t>(i))) && 
            //chs->handle_result(co_await chs->vars_float.send((float)test::init<float>(i))) && 
            //chs->handle_result(co_await chs->vars_double.send((double)test::init<double>(i))) && 
            //chs->handle_result(co_await chs->vars_char.send((char)test::init<char>(i))) && 
            //chs->handle_result(co_await chs->vars_voidp.send((void*)test::init<void*>(i))) && 
            //chs->handle_result(co_await chs->vars_std_string.send((std::string)test::init<std::string>(i))) && 
            //chs->handle_result(co_await chs->vars_CustomObject.send((CustomObject)test::init<CustomObject>(i))));
    }

    static inline hce::co<bool> receive_loop_op(channels* chs) {
        bool success=true;

        for(size_t i=0; success && i<100; ++i) {
            success = 
                chs->handle_result(
                    co_await scheduler<PARALLEL>::schedule(
                        receive_op(chs, i)));
        }

        co_return success;
    }

    static inline hce::co<bool> receive_op(channels* chs, size_t i) {
        co_return chs->handle_result(
            chs->handle_result(co_await chs->vars_int.receive((int)test::init<int>(i))));
            //chs->handle_result(co_await chs->vars_int.receive((int)test::init<int>(i))) && 
            //chs->handle_result(co_await chs->vars_unsigned_int.receive((unsigned int)test::init<unsigned int>(i))) && 
            //chs->handle_result(co_await chs->vars_size_t.receive((size_t)test::init<size_t>(i))) && 
            //chs->handle_result(co_await chs->vars_float.receive((float)test::init<float>(i))) && 
            //chs->handle_result(co_await chs->vars_double.receive((double)test::init<double>(i))) && 
            //chs->handle_result(co_await chs->vars_char.receive((char)test::init<char>(i))) && 
            //chs->handle_result(co_await chs->vars_voidp.receive((void*)test::init<void*>(i))) && 
            //chs->handle_result(co_await chs->vars_std_string.receive((std::string)test::init<std::string>(i))) && 
            //chs->handle_result(co_await chs->vars_CustomObject.receive((CustomObject)test::init<CustomObject>(i))));
    }
};

struct parallel_channels : public channels<true> {
    static inline std::string info_name() { 
        return "test::module::parallel_channels";
    }

    inline std::string name() const { return parallel_channels::info_name(); }
};

struct concurrent_channels : public channels<false> {
    static inline std::string info_name() { 
        return "test::module::concurrent_channels";
    }

    inline std::string name() const { return concurrent_channels::info_name(); }
};

struct blocking : public result_interface<10> {
    blocking()  { }
    virtual ~blocking(){}

    static inline std::string info_name() { 
        return "test::module::blocking";
    }

    inline std::string name() const { return blocking::info_name(); }

    inline hce::awt<bool> launch() {
        return hce::schedule(launch_blocking_ops(this));
    }

private:
    template <typename T>
    struct ops : public result_interface<300> {
        virtual ~ops(){}

        static inline std::string info_name() { 
            return "test::module::blocking::ops";
        }

        inline std::string name() const { return ops::info_name(); }

        static inline T block_done_immediately_T(T t, bool* ids_identical, std::thread::id parent_id) {
            HCE_HIGH_FUNCTION_BODY("block_done_immediately_T");
            *ids_identical = parent_id == std::this_thread::get_id();
            return std::move(t);
        }

        static inline T block_to_pop_queue_T(test::queue<T>* q, bool* ids_identical, std::thread::id parent_id) {
            HCE_HIGH_FUNCTION_BODY("block_for_queue_T");
            *ids_identical = parent_id == std::this_thread::get_id();
            return q->pop();
        }

        static inline void block_to_push_queue_T(test::queue<T>* q, size_t i, bool* ids_identical, std::thread::id parent_id) {
            HCE_HIGH_FUNCTION_BODY("block_to_send_queue_T");
            *ids_identical = parent_id == std::this_thread::get_id();
            q->push((T)test::init<T>(i));
        }

        static inline hce::co<bool> blocking_op(blocking* blk) {
            const std::thread::id id = std::this_thread::get_id();
            bool success = false;
            bool cont = true;
            bool ids_identical = false;
            bool pop_ids_identical = false;
            bool push_ids_identical = false;

            for(size_t i=0; cont && i<100; ++i) {
                success = 
                    blk->handle_result(
                        (T)test::init<T>(i) == 
                        co_await hce::block(
                            block_done_immediately_T,
                            (T)test::init<T>(i),
                            &ids_identical,
                            id));
                cont = success;

                if(success) {
                    test::queue<T> q;
                    auto pop_awt = 
                        hce::block(block_to_pop_queue_T, 
                                   &q, 
                                   &pop_ids_identical,
                                   id);
                    auto push_awt = 
                        hce::block(block_to_push_queue_T, 
                                   &q, 
                                   i,
                                   &push_ids_identical,
                                   id);

                    T t = co_await std::move(pop_awt);
                    bool pop_success = blk->handle_result(t == (T)test::init<T>(i));
                    co_await std::move(push_awt);
                    t = q.pop();
                    bool push_success = blk->handle_result(t == (T)test::init<T>(i));

                    success = pop_success && push_success;
                    cont = success;
                } 
            }

            co_return success;
        }
    };

    static inline hce::co<bool> launch_blocking_ops(blocking* blk) {
        co_return blk->handle_result(
            blk->handle_result(co_await hce::schedule(ops<int>::blocking_op(blk))) &&
            blk->handle_result(co_await hce::schedule(ops<unsigned int>::blocking_op(blk))) &&
            blk->handle_result(co_await hce::schedule(ops<size_t>::blocking_op(blk))) &&
            blk->handle_result(co_await hce::schedule(ops<float>::blocking_op(blk))) &&
            blk->handle_result(co_await hce::schedule(ops<double>::blocking_op(blk))) &&
            blk->handle_result(co_await hce::schedule(ops<char>::blocking_op(blk))) &&
            blk->handle_result(co_await hce::schedule(ops<void*>::blocking_op(blk))) &&
            blk->handle_result(co_await hce::schedule(ops<std::string>::blocking_op(blk))) &&
            blk->handle_result(co_await hce::schedule(ops<test::CustomObject>::blocking_op(blk))));
    }
};

struct timer : public result_interface<20> {
    // About 5.5 seconds optimally slept in timer() and then again in 
    // sleep() (total of 11 seconds). This adds a second as buffer
    static inline std::chrono::milliseconds expected_minimum_timeouts() { 
        return std::chrono::milliseconds(11000);
    }

    static inline std::chrono::milliseconds expected_total_timeouts() { 
        return std::chrono::milliseconds(12000);
    }

    virtual ~timer(){}

    static inline std::string info_name() { 
        return "test::module::timer";
    }

    inline std::string name() const { return timer::info_name(); }

    inline void wait_for_start() {
        start_queue_.pop();
    }

    inline void wait_for_end() {
        end_queue_.pop();
    }

    inline hce::awt<bool> launch() {
        return hce::schedule(launch_timer_ops(this));
    }

private: 
    test::queue<int> start_queue_;
    test::queue<int> end_queue_;

    static inline hce::co<bool> timer_op(const hce::chrono::duration dur) {
        hce::sid i;
        auto awt = hce::timer::start(i,dur);
        co_return co_await awt;
    }

    static inline hce::co<bool> sleep_op(const hce::chrono::duration dur) {
        co_await hce::sleep(dur);
        co_return true;
    }

    static inline hce::co<bool> launch_timer_ops(timer* tmr) {
        tmr->start_queue_.push(0);
        bool success = false;
        bool cont = true;

        for(size_t i=100; cont && i<1100; i+=100) {
            success = tmr->handle_result(
                co_await(hce::schedule(timer_op(std::chrono::milliseconds(i)))));
            cont = success;
        }

        if(success) {
            for(size_t i=100; cont && i<1100; i+=100) {
                success = tmr->handle_result(
                    co_await(hce::schedule(sleep_op(std::chrono::milliseconds(i)))));
                cont = success;
            }
        }

        tmr->end_queue_.push(0);
        co_return success;
    }
};

/*
 This object implements the shared interface between host and module for 
 communication in unit tests.
 */
struct interface {
    static constexpr unsigned int expected_code = 42;

    concurrent_channels conc_chs;
    parallel_channels para_chs;
    blocking blk;
    timer tmr;
};


}
}

#endif
