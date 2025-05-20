//SPDX-License-Identifier: Apache-2.0
//Author: Blayne Dennis 
#ifndef __HCE_COROUTINE_ENGINE_TEST_MODULE_HELPERS__
#define __HCE_COROUTINE_ENGINE_TEST_MODULE_HELPERS__

#include <mutex>
#include <sstream>
#include "hce.hpp"
#include "test_helpers.hpp"

namespace test {
namespace module {

inline constexpr size_t LOOP_OP_TX_MAX = 100;
inline constexpr size_t TYPE_VARIANT_COUNT = 9;

inline constexpr size_t CHANNEL_VARIANT_COUNT = 3;
inline constexpr size_t COMMUNICATOR_COUNT = 2; // 1 sender and receiver
inline constexpr size_t CHANNEL_RESULT_TOTAL = COMMUNICATOR_COUNT * TYPE_VARIANT_COUNT * CHANNEL_VARIANT_COUNT * LOOP_OP_TX_MAX;

inline constexpr size_t BLOCKING_VARIANT_COUNT = 3;
inline constexpr size_t BLOCKING_RESULT_TOTAL = TYPE_VARIANT_COUNT * BLOCKING_VARIANT_COUNT * LOOP_OP_TX_MAX;

inline constexpr size_t TIMER_TIMEOUT_INCREMENT_MS = 100;
inline constexpr size_t TIMER_TIMEOUT_LIMIT_MS = TIMER_TIMEOUT_INCREMENT_MS * 11;

enum struct command {
    error,
    send_concurrent_req,
    send_parallel_req,
    receive_concurrent_req,
    receive_parallel_req,
    blocking_req,
    timing_req
};

struct result : public hce::printable {
    result() :
        expected(0), 
        actual(0),
        error(0)
    { 
        HCE_HIGH_CONSTRUCTOR();
    }

    result(unsigned int exp, unsigned int act, unsigned int err) :
        expected(exp), 
        actual(act),
        error(err)
    { 
        HCE_HIGH_CONSTRUCTOR(exp, act, err);
    }

    virtual ~result() {
        HCE_HIGH_DESTRUCTOR();
    }

    static inline std::string info_name() { return "test::module::result"; }
    inline std::string name() const { return result::info_name(); }

    inline operator bool() const {
        std::string fname = "operator bool";
        bool success = this->validate_();

        if(!success) {
            HCE_ERROR_METHOD_BODY(fname, this->stringify_());
        } else {
            HCE_INFO_FUNCTION_BODY(fname, this->stringify_());
        }

        return success;
    }

    inline friend result operator+(const result& lhs, const result& rhs) {
        return result(
            lhs.expected + rhs.expected,
            lhs.actual + rhs.actual,
            lhs.error + rhs.error);
    }

    const unsigned int expected;
    const unsigned int actual;
    const unsigned int error;

private:
    // need sub-calculation 
    inline bool validate_() const { return (error == 0) && (expected == actual); }

    inline std::string stringify_() const { 
        std::stringstream ss;
        ss << "success["; 

        if(this->validate_()) {
            ss << "true";
        } else {
            ss << "false";
        }

        ss << "], expected[" << this->expected
           << "], actual[" << this->actual 
           << "], error[" << this->error << "]";
        return ss.str(); 
    }
};

template <unsigned int EXPECTED_SUCCESS_COUNT>
struct result_interface : public hce::printable {
    result_interface() {
        this->reset_results();
    }

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

    inline result validate_results() {
        std::lock_guard<std::mutex> lk(this->mtx_);

        if(!this->validated_) {
            this->validated_ = true;
            this->result_ = 
                std::make_unique<result>(EXPECTED_SUCCESS_COUNT, this->success_count_, this->error_count_);
        }

        return *(this->result_);
    }

    inline void reset_results() {
        this->validated_ = false;
        this->result_.reset();
        this->success_count_ = 0;
        this->error_count_ = 0;
    }

private:
    std::mutex mtx_;
    bool validated_;
    std::unique_ptr<result> result_;
    size_t success_count_;
    size_t error_count_;
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
struct channels : public result_interface<CHANNEL_RESULT_TOTAL> {
    channels() { }
    virtual ~channels(){}

    template <typename T>
    struct variants : public hce::printable {
        struct chan : public hce::printable {
            chan(int i) : 
                impl(hce::chan<T>::make(i)),
                chan_type_(i == 0 
                    ? "unbuffered"
                    : i > 0
                        ? "buffered"
                        : "unlimited")
            { }

            virtual ~chan(){}

            static inline std::string info_name() { 
                return variants<T>::info_name() + "::chan";
            }

            inline std::string name() const { return chan::info_name(); }
            inline std::string content() const { return chan_type_; }

            // the actual channel object
            hce::chan<T> impl;

            inline hce::awt<bool> send(channels<PARALLEL>* chs, const char* owner, T t) {
                return scheduler<PARALLEL>::schedule(send_and_result_op(chs, this, owner, t));
            }

            inline hce::awt<bool> receive(channels<PARALLEL>* chs, const char* owner, T t) {
                return scheduler<PARALLEL>::schedule(receive_and_result_op(chs, this, owner, t));
            }

        private:
            const char* chan_type_;

            static inline hce::co<bool> send_and_result_op(channels<PARALLEL>* chs, variants<T>::chan* ch, const char* owner, T t) {
                std::string fname = ch->name() + "::send_and_result_op@" + owner;
                bool success = false;
                success = chs->handle_result(co_await ch->impl.send(t));

                if(!success) {
                    HCE_ERROR_FUNCTION_BODY(fname, "channel[", ch->impl,"] send fail");
                }

                co_return success;
            }

            static inline hce::co<bool> receive_and_result_op(channels<PARALLEL>* chs, variants<T>::chan* ch, const char* owner, T t) {
                std::string fname = ch->name() + "::receive_and_result_op@" + owner;
                bool success = false;
                T t2;
                success = chs->handle_result(co_await ch->impl.recv(t2));

                if(success) {
                    if(t != t2) {
                        HCE_ERROR_FUNCTION_BODY(fname, "value mismatch, t[", t, "] != t2[", t2, "]");
                        success = false;
                    }
                } else {
                    HCE_ERROR_FUNCTION_BODY(fname, "channel[", ch->impl,"] receive fail");
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

        chan unbuf;
        chan buf;
        chan unlim;

        inline hce::awt<bool> send(channels<PARALLEL>* chs, const char* owner, T t) {
            return scheduler<PARALLEL>::schedule(send_op(chs, this, owner, t));
        }

        inline hce::awt<bool> receive(channels<PARALLEL>* chs, const char* owner, T t) {
            return scheduler<PARALLEL>::schedule(receive_op(chs, this, owner, t));
        }

    private:
        static inline hce::co<bool> send_op(channels<PARALLEL>* chs, variants<T>* vars, const char* owner, T t) {
            auto awt_unbuf = vars->unbuf.send(chs, owner, t);
            auto awt_buf = vars->buf.send(chs, owner, t);
            auto awt_unlim = vars->unlim.send(chs, owner, t);
            co_return 
                co_await awt_unbuf && 
                co_await awt_buf && 
                co_await awt_unlim;
        }

        static inline hce::co<bool> receive_op(channels<PARALLEL>* chs, variants<T>* vars, const char* owner, T t) {
            auto awt_unbuf = vars->unbuf.receive(chs, owner, t);
            auto awt_buf = vars->buf.receive(chs, owner, t);
            auto awt_unlim = vars->unlim.receive(chs, owner, t);
            co_return 
                co_await awt_unbuf && 
                co_await awt_buf && 
                co_await awt_unlim;
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
        return PARALLEL ? "test::module::parchans" 
                        : "test::module::conchans";
    }

    inline std::string name() const {
        return channels<PARALLEL>::info_name();
    }

    inline hce::awt<bool> send(const char* owner) {
        return scheduler<PARALLEL>::schedule(send_loop_op(this, owner));
    }

    inline hce::awt<bool> receive(const char* owner) {
        return scheduler<PARALLEL>::schedule(receive_loop_op(this, owner));
    }

private:
    // expects 1100 successes
    static inline hce::co<bool> send_loop_op(channels<PARALLEL>* chs, const char* owner) {
        std::string fname = chs->name() + "::send_loop_op@" + owner;
        bool success=true;

        for(size_t i=0; success && i<LOOP_OP_TX_MAX; ++i) {
            success = co_await scheduler<PARALLEL>::schedule(send_op(chs, owner, i));

            if(!success) {
                HCE_ERROR_FUNCTION_BODY(fname, "send_op(", chs, ", ", i,") failed");
            }
        }

        co_return success;
    }

    static inline hce::co<bool> send_op(channels<PARALLEL>* chs, const char* owner, size_t i) {
        co_return 
            co_await chs->vars_int.send(chs, owner, (int)test::init<int>(i)) && 
            co_await chs->vars_unsigned_int.send(chs, owner, (unsigned int)test::init<unsigned int>(i)) && 
            co_await chs->vars_size_t.send(chs, owner, (size_t)test::init<size_t>(i)) && 
            co_await chs->vars_float.send(chs, owner, (float)test::init<float>(i)) && 
            co_await chs->vars_double.send(chs, owner, (double)test::init<double>(i)) && 
            co_await chs->vars_char.send(chs, owner, (char)test::init<char>(i)) && 
            co_await chs->vars_voidp.send(chs, owner, (void*)test::init<void*>(i)) && 
            co_await chs->vars_std_string.send(chs, owner, (std::string)test::init<std::string>(i)) && 
            co_await chs->vars_CustomObject.send(chs, owner, (CustomObject)test::init<CustomObject>(i));
    }

    static inline hce::co<bool> receive_loop_op(channels<PARALLEL>* chs, const char* owner) {
        std::string fname = chs->name() + "::receive_loop_op@" + owner;
        bool success=true;

        for(size_t i=0; success && i<LOOP_OP_TX_MAX; ++i) {
            success = co_await scheduler<PARALLEL>::schedule(receive_op(chs, owner, i));

            if(!success) {
                HCE_ERROR_FUNCTION_BODY(fname, "receive_op(", chs, ", ", i,") failed");
            }
        }

        co_return success;
    }

    static inline hce::co<bool> receive_op(channels<PARALLEL>* chs, const char* owner, size_t i) {
        co_return 
            co_await chs->vars_int.receive(chs, owner, (int)test::init<int>(i)) && 
            co_await chs->vars_unsigned_int.receive(chs, owner, (unsigned int)test::init<unsigned int>(i)) && 
            co_await chs->vars_size_t.receive(chs, owner, (size_t)test::init<size_t>(i)) && 
            co_await chs->vars_float.receive(chs, owner, (float)test::init<float>(i)) && 
            co_await chs->vars_double.receive(chs, owner, (double)test::init<double>(i)) && 
            co_await chs->vars_char.receive(chs, owner, (char)test::init<char>(i)) && 
            co_await chs->vars_voidp.receive(chs, owner, (void*)test::init<void*>(i)) && 
            co_await chs->vars_std_string.receive(chs, owner, (std::string)test::init<std::string>(i)) && 
            co_await chs->vars_CustomObject.receive(chs, owner, (CustomObject)test::init<CustomObject>(i));
    }
};

typedef channels<true> parchans;
typedef channels<false> conchans;

struct blocking : public result_interface<BLOCKING_RESULT_TOTAL> {
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
    struct ops : public hce::printable {
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

        static inline hce::co<bool> blocking_and_result_op(blocking* blk) {
            std::string fname = blk->name() + "::blocking_and_result_op";
            const std::thread::id id = std::this_thread::get_id();
            bool success = false;

            for(size_t i=0; i<LOOP_OP_TX_MAX; ++i) {
                bool ids_identical = false;
                bool pop_ids_identical = false;
                bool push_ids_identical = false;

                T t = co_await hce::block(
                            block_done_immediately_T,
                            (T)test::init<T>(i),
                            &ids_identical,
                            id);

                bool immediate_t_same = (t == (T)test::init<T>(i));

                if(!immediate_t_same) {
                    HCE_ERROR_FUNCTION_BODY(fname, "done immediately T mismatch, expected[",(T)test::init<T>(i),"], actual[",t,"");
                }

                if(ids_identical) {
                    HCE_ERROR_FUNCTION_BODY(fname, "done immediately ids were identical");
                }

                bool immediate_success = blk->handle_result(immediate_t_same && !ids_identical);

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

                co_await std::move(push_awt);
                t = co_await std::move(pop_awt);
                bool pop_t_same = (t == (T)test::init<T>(i));

                if(!pop_t_same) {
                    HCE_ERROR_FUNCTION_BODY(fname, "queue pop T mismatch, expected[",(T)test::init<T>(i),"], actual[",t,"");
                }

                if(pop_ids_identical) {
                    HCE_ERROR_FUNCTION_BODY(fname, "queue pop ids were identical");
                }

                if(push_ids_identical) {
                    HCE_ERROR_FUNCTION_BODY(fname, "queue push ids were identical");
                }

                bool pop_success = blk->handle_result(pop_t_same && !pop_ids_identical);
                bool push_success = blk->handle_result(!push_ids_identical);
                success = immediate_success && pop_success && push_success;

                if(!success) {
                    break;
                }
            }

            co_return success;
        }
    };

    static inline hce::co<bool> launch_blocking_ops(blocking* blk) {
        co_return 
            co_await hce::schedule(ops<int>::blocking_and_result_op(blk)) &&
            co_await hce::schedule(ops<unsigned int>::blocking_and_result_op(blk)) &&
            co_await hce::schedule(ops<size_t>::blocking_and_result_op(blk)) &&
            co_await hce::schedule(ops<float>::blocking_and_result_op(blk)) &&
            co_await hce::schedule(ops<double>::blocking_and_result_op(blk)) &&
            co_await hce::schedule(ops<char>::blocking_and_result_op(blk)) &&
            co_await hce::schedule(ops<void*>::blocking_and_result_op(blk)) &&
            co_await hce::schedule(ops<std::string>::blocking_and_result_op(blk)) &&
            co_await hce::schedule(ops<test::CustomObject>::blocking_and_result_op(blk));
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

        for(size_t i=TIMER_TIMEOUT_INCREMENT_MS; 
            cont && i<TIMER_TIMEOUT_LIMIT_MS; 
            i+=TIMER_TIMEOUT_INCREMENT_MS) 
        {
            success = tmr->handle_result(
                co_await(hce::schedule(timer_op(std::chrono::milliseconds(i)))));
            cont = success;
        }

        if(success) {
            for(size_t i=TIMER_TIMEOUT_INCREMENT_MS; 
                cont && i<TIMER_TIMEOUT_LIMIT_MS; 
                i+=TIMER_TIMEOUT_INCREMENT_MS) 
            {
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

    inline void init() {
        comch = hce::chan<command>::make();
        resch = hce::chan<bool>::make();
        cchs.reset_results();
        pchs.reset_results();
        blk.reset_results();
        tmr.reset_results();
    }

    // access the global interface instance
    static interface& global();

    // host->module command channel
    hce::chan<command> comch;

    // module->host result channel
    hce::chan<bool> resch;

    // used to implement commands
    conchans cchs;
    parchans pchs;
    blocking blk;
    timer tmr;
};


}
}

#endif
