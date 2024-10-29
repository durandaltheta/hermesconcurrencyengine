//SPDX-License-Identifier: Apache-2.0
//Author: Blayne Dennis 
#include <deque>
#include <string>
#include <thread>
#include <chrono>
#include <vector>

#include "loguru.hpp"
#include "atomic.hpp"
#include "scheduler.hpp"

#include <gtest/gtest.h> 
#include "test_helpers.hpp"

namespace test {
namespace scheduler {

inline hce::co<void> co_void() { co_return; }

template <typename T>
hce::co<void> co_push_T(test::queue<T>& q, T t) {
    q.push(t);
    co_return;
}

template <typename T>
inline hce::co<T> co_return_T(T t) {
    co_return t;
}

template <typename T>
hce::co<T> co_push_T_return_T(test::queue<T>& q, T t) {
    q.push(t);
    co_return t;
}

template <typename T>
hce::co<T> co_push_T_yield_void_and_return_T(test::queue<T>& q, T t) {
    q.push(t);
    co_await hce::yield<void>();
    co_return t;
}

template <typename T>
hce::co<T> co_push_T_yield_T_and_return_T(test::queue<T>& q, T t) {
    q.push(t);
    co_return co_await hce::yield<T>(t);
}

inline hce::co<void> co_scheduler_in_check(test::queue<void*>& q) { 
    q.push(hce::scheduler::in() ? (void*)1 : (void*)0);
    co_return;
}

inline hce::co<void> co_scheduler_local_check(test::queue<void*>& q) { 
    q.push(&(hce::scheduler::local()));
    co_return;
}

inline hce::co<void> co_scheduler_global_check(test::queue<void*>& q) { 
    q.push(&(hce::scheduler::global()));
    co_return;
}

}
}

TEST(scheduler, make_with_lifecycle) {
    std::shared_ptr<hce::scheduler> sch;
    std::unique_ptr<hce::scheduler::lifecycle> lf;

    {
        lf = hce::scheduler::make();
        sch = lf->scheduler();
        EXPECT_TRUE(sch);
        EXPECT_EQ(hce::scheduler::state::executing, sch->status());
    }
    
    lf.reset();

    // scheduler should be shutdown
    EXPECT_EQ(hce::scheduler::state::halted, sch->status());
    sch.reset();

    {
        lf = hce::scheduler::make();
        sch = lf->scheduler();
        EXPECT_TRUE(sch);
        EXPECT_EQ(hce::scheduler::state::executing, sch->status());
        lf->suspend();
        EXPECT_EQ(hce::scheduler::state::suspended, sch->status());
        lf->resume();
        EXPECT_EQ(hce::scheduler::state::executing, sch->status());
    }

    lf.reset();

    EXPECT_EQ(hce::scheduler::state::halted, sch->status());
}

TEST(scheduler, conversions) {
    std::shared_ptr<hce::scheduler> sch;

    {
        auto lf = hce::scheduler::make();
        sch = lf->scheduler();
        EXPECT_EQ(hce::scheduler::state::executing, sch->status());

        hce::scheduler& sch_ref = *sch;
        EXPECT_EQ(&sch_ref, sch.get());

        std::shared_ptr<hce::scheduler> sch_cpy = *sch;
        EXPECT_EQ(sch_cpy, sch);

        std::weak_ptr<hce::scheduler> sch_weak = *sch;
        EXPECT_EQ(sch_weak.lock(), sch);
    }

    EXPECT_EQ(hce::scheduler::state::halted, sch->status());
}

namespace test {
namespace scheduler {

template <typename T>
size_t schedule_T(std::function<hce::co<void>(test::queue<T>& q, T t)> Coroutine) {
    std::string T_name = hce::type::name<T>();

    HCE_INFO_LOG("schedule_T<%s>",T_name.c_str());

    size_t success_count = 0;

    // schedule individually
    {
        test::queue<T> q;
        auto lf = hce::scheduler::make();
        std::shared_ptr<hce::scheduler> sch = lf->scheduler();
        HCE_INFO_LOG("schedule_T<%s> started scheduler",T_name.c_str());

        auto awt1 = sch->schedule(Coroutine(q,test::init<T>(3)));
        auto awt2 = sch->schedule(Coroutine(q,test::init<T>(2)));
        auto awt3 = sch->schedule(Coroutine(q,test::init<T>(1)));
        
        HCE_INFO_LOG("schedule_T<%s> launched coroutines",T_name.c_str());

        try {
            EXPECT_EQ((T)test::init<T>(3), q.pop());
            EXPECT_EQ((T)test::init<T>(2), q.pop());
            EXPECT_EQ((T)test::init<T>(1), q.pop());

            ++success_count;
            HCE_INFO_LOG("schedule_T<%s> received values",T_name.c_str());
        } catch(const std::exception& e) {
            LOG_F(ERROR, e.what());
        }

        HCE_INFO_LOG("schedule_T<%s> end of scope",T_name.c_str());
    }

    HCE_INFO_LOG("schedule_T<%s> done",T_name.c_str());

    return success_count;
}

}
}

TEST(scheduler, schedule) {
    // the count of schedule subtests we expect to complete without throwing 
    // exceptions
    const size_t expected = 1;
    EXPECT_EQ(expected, test::scheduler::schedule_T<int>(test::scheduler::co_push_T<int>));
    EXPECT_EQ(expected, test::scheduler::schedule_T<unsigned int>(test::scheduler::co_push_T<unsigned int>));
    EXPECT_EQ(expected, test::scheduler::schedule_T<size_t>(test::scheduler::co_push_T<size_t>));
    EXPECT_EQ(expected, test::scheduler::schedule_T<float>(test::scheduler::co_push_T<float>));
    EXPECT_EQ(expected, test::scheduler::schedule_T<double>(test::scheduler::co_push_T<double>));
    EXPECT_EQ(expected, test::scheduler::schedule_T<char>(test::scheduler::co_push_T<char>));
    EXPECT_EQ(expected, test::scheduler::schedule_T<void*>(test::scheduler::co_push_T<void*>));
    EXPECT_EQ(expected, test::scheduler::schedule_T<std::string>(test::scheduler::co_push_T<std::string>));
    EXPECT_EQ(expected, test::scheduler::schedule_T<test::CustomObject>(test::scheduler::co_push_T<test::CustomObject>));
}

/*
test::scheduler::co_push_T_yield_void_and_return_T
test::scheduler::co_push_T_yield_T_and_return_T
*/
TEST(scheduler, schedule_yield) {
    // the count of schedule subtests we expect to complete without throwing 
    // exceptions
    const size_t expected = 1;

    // yield then return
    {
        EXPECT_EQ(expected, test::scheduler::schedule_T<int>(test::scheduler::co_push_T_yield_void_and_return_T<int>));
        EXPECT_EQ(expected, test::scheduler::schedule_T<unsigned int>(test::scheduler::co_push_T_yield_void_and_return_T<unsigned int>));
        EXPECT_EQ(expected, test::scheduler::schedule_T<size_t>(test::scheduler::co_push_T_yield_void_and_return_T<size_t>));
        EXPECT_EQ(expected, test::scheduler::schedule_T<float>(test::scheduler::co_push_T_yield_void_and_return_T<float>));
        EXPECT_EQ(expected, test::scheduler::schedule_T<double>(test::scheduler::co_push_T_yield_void_and_return_T<double>));
        EXPECT_EQ(expected, test::scheduler::schedule_T<char>(test::scheduler::co_push_T_yield_void_and_return_T<char>));
        EXPECT_EQ(expected, test::scheduler::schedule_T<void*>(test::scheduler::co_push_T_yield_void_and_return_T<void*>));
        EXPECT_EQ(expected, test::scheduler::schedule_T<std::string>(test::scheduler::co_push_T_yield_void_and_return_T<std::string>));
        EXPECT_EQ(expected, test::scheduler::schedule_T<test::CustomObject>(test::scheduler::co_push_T_yield_void_and_return_T<test::CustomObject>));
    }

    // yield *into* a return
    {
        EXPECT_EQ(expected, test::scheduler::schedule_T<int>(test::scheduler::co_push_T_yield_T_and_return_T<int>));
        EXPECT_EQ(expected, test::scheduler::schedule_T<unsigned int>(test::scheduler::co_push_T_yield_T_and_return_T<unsigned int>));
        EXPECT_EQ(expected, test::scheduler::schedule_T<size_t>(test::scheduler::co_push_T_yield_T_and_return_T<size_t>));
        EXPECT_EQ(expected, test::scheduler::schedule_T<float>(test::scheduler::co_push_T_yield_T_and_return_T<float>));
        EXPECT_EQ(expected, test::scheduler::schedule_T<double>(test::scheduler::co_push_T_yield_T_and_return_T<double>));
        EXPECT_EQ(expected, test::scheduler::schedule_T<char>(test::scheduler::co_push_T_yield_T_and_return_T<char>));
        EXPECT_EQ(expected, test::scheduler::schedule_T<void*>(test::scheduler::co_push_T_yield_T_and_return_T<void*>));
        EXPECT_EQ(expected, test::scheduler::schedule_T<std::string>(test::scheduler::co_push_T_yield_T_and_return_T<std::string>));
        EXPECT_EQ(expected, test::scheduler::schedule_T<test::CustomObject>(test::scheduler::co_push_T_yield_T_and_return_T<test::CustomObject>));
    }
}

TEST(scheduler, schedule_and_thread_locals) {
    {
        test::queue<void*> sch_q;
        hce::scheduler* global_sch = &(hce::scheduler::global());
        auto lf = hce::scheduler::make();
        std::shared_ptr<hce::scheduler> sch = lf->scheduler();

        try {
            auto awt1 = sch->schedule(test::scheduler::co_scheduler_in_check(sch_q));
            auto awt2 = sch->schedule(test::scheduler::co_scheduler_local_check(sch_q));
            auto awt3 = sch->schedule(test::scheduler::co_scheduler_global_check(sch_q));

            EXPECT_NE(nullptr, sch_q.pop());

            hce::scheduler* recv = (hce::scheduler*)(sch_q.pop());
            EXPECT_EQ(sch.get(), recv);
            EXPECT_NE(global_sch, recv);

            recv = (hce::scheduler*)(sch_q.pop());
            EXPECT_NE(sch.get(), recv);
            EXPECT_EQ(global_sch, recv);
        } catch(const std::exception& e) {
            LOG_F(ERROR, e.what());
        }
    }
}

namespace test {
namespace scheduler {

template <typename T>
size_t join_schedule_T() {
    std::string T_name = []() -> std::string {
        std::stringstream ss;
        ss << typeid(T).name();
        return ss.str();
    }();

    HCE_INFO_LOG("join_schedule_T<%s>",T_name.c_str());
    size_t success_count = 0;

    // schedule individually
    {
        auto lf = hce::scheduler::make();
        std::shared_ptr<hce::scheduler> sch = lf->scheduler();
        std::deque<hce::awt<T>> schedules;

        schedules.push_back(sch->schedule(
            co_return_T<T>(test::init<T>(3))));
        schedules.push_back(sch->schedule(
            co_return_T<T>(test::init<T>(2))));
        schedules.push_back(sch->schedule(
            co_return_T<T>(test::init<T>(1))));

        try {
            T result = schedules.front();
            schedules.pop_front();
            EXPECT_EQ((T)test::init<T>(3), result);
            result = schedules.front();
            schedules.pop_front();
            EXPECT_EQ((T)test::init<T>(2), result);
            result = schedules.front();
            schedules.pop_front();
            EXPECT_EQ((T)test::init<T>(1), result);

            ++success_count;
        } catch(const std::exception& e) {
            LOG_F(ERROR, e.what());
        }
    } 

    // schedule individually in reverse order
    {
        auto lf = hce::scheduler::make();
        std::shared_ptr<hce::scheduler> sch = lf->scheduler();
        std::deque<hce::awt<T>> schedules;

        schedules.push_back(sch->schedule(co_return_T<T>(test::init<T>(3))));
        schedules.push_back(sch->schedule(co_return_T<T>(test::init<T>(2))));
        schedules.push_back(sch->schedule(co_return_T<T>(test::init<T>(1))));

        try {
            T result = schedules.back();
            schedules.pop_back();
            EXPECT_EQ((T)test::init<T>(1), result);
            result = schedules.back();
            schedules.pop_back();
            EXPECT_EQ((T)test::init<T>(2), result);
            result = schedules.back();
            schedules.pop_back();
            EXPECT_EQ((T)test::init<T>(3), result);

            ++success_count;
        } catch(const std::exception& e) {
            LOG_F(ERROR, e.what());
        }
    }

    // schedule void
    {
        auto lf = hce::scheduler::make();
        std::shared_ptr<hce::scheduler> sch = lf->scheduler();
        std::deque<hce::awt<void>> schedules;

        schedules.push_back(sch->schedule(co_void()));
        schedules.push_back(sch->schedule(co_void()));
        schedules.push_back(sch->schedule(co_void()));

        try {
            schedules.pop_front();
            schedules.pop_front();
            schedules.pop_front();

            ++success_count;
        } catch(const std::exception& e) {
            LOG_F(ERROR, e.what());
        }
    }

    return success_count;
}

}
}

TEST(scheduler, join_schedule) {
    const size_t expected = 3;
    EXPECT_EQ(expected, test::scheduler::join_schedule_T<int>());
    EXPECT_EQ(expected, test::scheduler::join_schedule_T<unsigned int>());
    EXPECT_EQ(expected, test::scheduler::join_schedule_T<size_t>());
    EXPECT_EQ(expected, test::scheduler::join_schedule_T<float>());
    EXPECT_EQ(expected, test::scheduler::join_schedule_T<double>());
    EXPECT_EQ(expected, test::scheduler::join_schedule_T<char>());
    EXPECT_EQ(expected, test::scheduler::join_schedule_T<void*>());
    EXPECT_EQ(expected, test::scheduler::join_schedule_T<std::string>());
    EXPECT_EQ(expected, test::scheduler::join_schedule_T<test::CustomObject>());
}
