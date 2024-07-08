//SPDX-License-Identifier: Apache-2.0
//Author: Blayne Dennis 
#include <deque>
#include <thread>
#include <chrono>
#include <vector>
#include <list>
#include <forward_list>

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

    {
        std::unique_ptr<hce::scheduler::lifecycle> lf;
        sch = hce::scheduler::make(lf);
        EXPECT_EQ(hce::scheduler::state::ready, sch->status());
    }

    EXPECT_EQ(hce::scheduler::state::halted, sch->status());

    {
        std::unique_ptr<hce::scheduler::lifecycle> lf;
        sch = hce::scheduler::make(lf);
        EXPECT_EQ(hce::scheduler::state::ready, sch->status());
        lf->suspend();
        EXPECT_EQ(hce::scheduler::state::suspended, sch->status());
        lf->resume();
        EXPECT_EQ(hce::scheduler::state::ready, sch->status());
    }

    EXPECT_EQ(hce::scheduler::state::halted, sch->status());
}

TEST(scheduler, conversions) {
    std::shared_ptr<hce::scheduler> sch;

    {
        std::unique_ptr<hce::scheduler::lifecycle> lf;
        sch = hce::scheduler::make(lf);
        EXPECT_EQ(hce::scheduler::state::ready, sch->status());

        hce::scheduler& sch_ref = *sch;
        EXPECT_EQ(&sch_ref, sch.get());

        std::shared_ptr<hce::scheduler> sch_cpy = *sch;
        EXPECT_EQ(sch_cpy, sch);

        std::weak_ptr<hce::scheduler> sch_weak = *sch;
        EXPECT_EQ(sch_weak.lock(), sch);
    }

    EXPECT_EQ(hce::scheduler::state::halted, sch->status());
}

TEST(scheduler, install) {
    // halt with lifecycle 
    {
        std::unique_ptr<hce::scheduler::lifecycle> lf;
        auto sch = hce::scheduler::make(lf);
        EXPECT_EQ(hce::scheduler::state::ready, sch->status());

        std::thread thd([&]{ sch->install(); });
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        EXPECT_EQ(hce::scheduler::state::running, sch->status());

        lf.reset();
        EXPECT_EQ(hce::scheduler::state::halted, sch->status());
        thd.join();
    }

    // halt during suspend
    {
        test::queue<hce::scheduler::state> state_q;
        std::unique_ptr<hce::scheduler::lifecycle> lf;
        auto sch = hce::scheduler::make(lf);
        auto config = hce::scheduler::config::make();

        // install 1 init handler
        config->on_init.install([&]{ 
            state_q.push(hce::scheduler::state::ready);
        });

        // install 3 suspend handlers
        for(size_t i=0; i<3; ++i) {
            config->on_suspend.install([&]{ 
                state_q.push(hce::scheduler::state::suspended);
            });
        }

        // install 2 halt handlers
        for(size_t i=0; i<2; ++i) {
            config->on_halt.install([&]{ 
                state_q.push(hce::scheduler::state::halted);
            });
        }

        std::thread thd([&]{ sch->install(std::move(config)); });

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        EXPECT_EQ(hce::scheduler::state::running, sch->status());

        EXPECT_EQ(hce::scheduler::state::ready, state_q.pop());

        lf->suspend();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        EXPECT_EQ(hce::scheduler::state::suspended, state_q.pop());
        EXPECT_EQ(hce::scheduler::state::suspended, state_q.pop());
        EXPECT_EQ(hce::scheduler::state::suspended, state_q.pop());
        
        lf->resume();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        lf->suspend();
        EXPECT_EQ(hce::scheduler::state::suspended, state_q.pop());
        EXPECT_EQ(hce::scheduler::state::suspended, state_q.pop());
        EXPECT_EQ(hce::scheduler::state::suspended, state_q.pop());

        lf.reset();
        EXPECT_EQ(hce::scheduler::state::halted, state_q.pop());
        EXPECT_EQ(hce::scheduler::state::halted, state_q.pop());
        thd.join();
    }

    // halt during run
    {
        test::queue<hce::scheduler::state> state_q;
        std::unique_ptr<hce::scheduler::lifecycle> lf;
        auto sch = hce::scheduler::make(lf);
        auto config = hce::scheduler::config::make();

        // install 1 init handler
        config->on_init.install([&]{ 
            state_q.push(hce::scheduler::state::ready);
        });

        // install 3 suspend handlers
        for(size_t i=0; i<3; ++i) {
            config->on_suspend.install([&]{ 
                state_q.push(hce::scheduler::state::suspended);
            });
        }

        // install 2 halt handlers
        for(size_t i=0; i<2; ++i) {
            config->on_halt.install([&]{ 
                state_q.push(hce::scheduler::state::halted);
            });
        }

        std::thread thd([&]{ sch->install(std::move(config)); });

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        EXPECT_EQ(hce::scheduler::state::running, sch->status());

        EXPECT_EQ(hce::scheduler::state::ready, state_q.pop());

        lf->suspend();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        EXPECT_EQ(hce::scheduler::state::suspended, state_q.pop());
        EXPECT_EQ(hce::scheduler::state::suspended, state_q.pop());
        EXPECT_EQ(hce::scheduler::state::suspended, state_q.pop());
        
        lf->resume();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        lf.reset();
        EXPECT_EQ(hce::scheduler::state::halted, state_q.pop());
        EXPECT_EQ(hce::scheduler::state::halted, state_q.pop());
        thd.join();
    }
}

namespace test {
namespace scheduler {

template <typename T>
size_t schedule_T(std::function<hce::co<T>(test::queue<T>& q, T t)> Coroutine) {
    std::string T_name = []() -> std::string {
        std::stringstream ss;
        ss << typeid(T).name();
        return ss.str();
    }();

    HCE_INFO_LOG("schedule_T<%s>",T_name.c_str());

    size_t success_count = 0;

    // schedule individually
    {
        test::queue<T> q;
        std::unique_ptr<hce::scheduler::lifecycle> lf;
        auto sch = hce::scheduler::make(lf);
        std::thread thd([&]{ sch->install(); });

        sch->schedule(Coroutine(q,test::init<T>(3)));
        sch->schedule(Coroutine(q,test::init<T>(2)));
        sch->schedule(Coroutine(q,test::init<T>(1)));

        try {
            EXPECT_EQ((T)test::init<T>(3), q.pop());
            EXPECT_EQ((T)test::init<T>(2), q.pop());
            EXPECT_EQ((T)test::init<T>(1), q.pop());

            lf.reset();
            thd.join();
            ++success_count;
        } catch(const std::exception& e) {
            LOG_F(ERROR, e.what());
        }
    }

    // schedule group
    {
        test::queue<T> q;
        std::unique_ptr<hce::scheduler::lifecycle> lf;
        auto sch = hce::scheduler::make(lf);
        std::thread thd([&]{ sch->install(); });

        sch->schedule(Coroutine(q,test::init<T>(3)),
                      Coroutine(q,test::init<T>(2)),
                      Coroutine(q,test::init<T>(1)));

        try {
            EXPECT_EQ((T)test::init<T>(3), q.pop());
            EXPECT_EQ((T)test::init<T>(2), q.pop());
            EXPECT_EQ((T)test::init<T>(1), q.pop());

            lf.reset();
            thd.join();
            ++success_count;
        } catch(const std::exception& e) {
            LOG_F(ERROR, e.what());
        }
    }

    // schedule group of base hce::coroutines
    {
        test::queue<T> q;
        std::unique_ptr<hce::scheduler::lifecycle> lf;
        auto sch = hce::scheduler::make(lf);
        std::thread thd([&]{ sch->install(); });

        sch->schedule(hce::coroutine(Coroutine(q,test::init<T>(3))),
                      hce::coroutine(Coroutine(q,test::init<T>(2))),
                      hce::coroutine(Coroutine(q,test::init<T>(1))));

        try {
            EXPECT_EQ((T)test::init<T>(3), q.pop());
            EXPECT_EQ((T)test::init<T>(2), q.pop());
            EXPECT_EQ((T)test::init<T>(1), q.pop());

            lf.reset();
            thd.join();
            ++success_count;
        } catch(const std::exception& e) {
            LOG_F(ERROR, e.what());
        }
    }

    // schedule group of different coroutine signatures
    {
        test::queue<T> q;
        std::unique_ptr<hce::scheduler::lifecycle> lf;
        auto sch = hce::scheduler::make(lf);
        std::thread thd([&]{ sch->install(); });

        sch->schedule(Coroutine(q,test::init<T>(3)),
                      hce::coroutine(Coroutine(q,test::init<T>(2))),
                      test::scheduler::co_push_T_return_T<T>(q,test::init<T>(1)));

        try {
            EXPECT_EQ((T)test::init<T>(3), q.pop());
            EXPECT_EQ((T)test::init<T>(2), q.pop());
            EXPECT_EQ((T)test::init<T>(1), q.pop());

            lf.reset();
            thd.join();
            ++success_count;
        } catch(const std::exception& e) {
            LOG_F(ERROR, e.what());
        }
    }

    // schedule group and single
    {
        test::queue<T> q;
        std::unique_ptr<hce::scheduler::lifecycle> lf;
        auto sch = hce::scheduler::make(lf);
        std::thread thd([&]{ sch->install(); });

        sch->schedule(Coroutine(q,test::init<T>(3)),
                      Coroutine(q,test::init<T>(2)));
        sch->schedule(Coroutine(q,test::init<T>(1)));

        try {
            EXPECT_EQ((T)test::init<T>(3), q.pop());
            EXPECT_EQ((T)test::init<T>(2), q.pop());
            EXPECT_EQ((T)test::init<T>(1), q.pop());

            lf.reset();
            thd.join();
            ++success_count;
        } catch(const std::exception& e) {
            LOG_F(ERROR, e.what());
        }
    }

    // schedule in a vector
    {
        test::queue<T> q;
        std::unique_ptr<hce::scheduler::lifecycle> lf;
        auto sch = hce::scheduler::make(lf);
        std::thread thd([&]{ sch->install(); });

        std::vector<hce::co<void>> cos;
        cos.push_back(Coroutine(q,test::init<T>(3)));
        cos.push_back(Coroutine(q,test::init<T>(2)));
        cos.push_back(Coroutine(q,test::init<T>(1)));

        try {
            sch->schedule(std::move(cos));

            EXPECT_EQ((T)test::init<T>(3), q.pop());
            EXPECT_EQ((T)test::init<T>(2), q.pop());
            EXPECT_EQ((T)test::init<T>(1), q.pop());

            lf.reset();
            thd.join();
            ++success_count;
        } catch(const std::exception& e) {
            LOG_F(ERROR, e.what());
        }
    }

    // schedule in a list
    {
        test::queue<T> q;
        std::unique_ptr<hce::scheduler::lifecycle> lf;
        auto sch = hce::scheduler::make(lf);
        std::thread thd([&]{ sch->install(); });

        std::list<hce::co<void>> cos;
        cos.push_back(Coroutine(q,test::init<T>(3)));
        cos.push_back(Coroutine(q,test::init<T>(2)));
        cos.push_back(Coroutine(q,test::init<T>(1)));

        try {
            sch->schedule(std::move(cos));

            EXPECT_EQ((T)test::init<T>(3), q.pop());
            EXPECT_EQ((T)test::init<T>(2), q.pop());
            EXPECT_EQ((T)test::init<T>(1), q.pop());

            lf.reset();
            thd.join();
            ++success_count;
        } catch(const std::exception& e) {
            LOG_F(ERROR, e.what());
        }
    }

    // schedule in a forward_list
    {
        test::queue<T> q;
        std::unique_ptr<hce::scheduler::lifecycle> lf;
        auto sch = hce::scheduler::make(lf);
        std::thread thd([&]{ sch->install(); });

        std::forward_list<hce::co<void>> cos;
        cos.push_front(Coroutine(q,test::init<T>(3)));
        cos.push_front(Coroutine(q,test::init<T>(2)));
        cos.push_front(Coroutine(q,test::init<T>(1)));

        try {
            sch->schedule(std::move(cos));

            EXPECT_EQ((T)test::init<T>(1), q.pop());
            EXPECT_EQ((T)test::init<T>(2), q.pop());
            EXPECT_EQ((T)test::init<T>(3), q.pop());

            lf.reset();
            thd.join();
            ++success_count;
        } catch(const std::exception& e) {
            LOG_F(ERROR, e.what());
        }
    }

    return success_count;
}

}
}

TEST(scheduler, schedule) {
    // the count of schedule subtests we expect to complete without throwing 
    // exceptions
    const size_t expected = 8;
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
    const size_t expected = 8;

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
        std::unique_ptr<hce::scheduler::lifecycle> lf;
        auto sch = hce::scheduler::make(lf);
        std::thread thd([&]{ sch->install(); });

        try {
            sch->schedule(test::scheduler::co_scheduler_in_check(sch_q));
            sch->schedule(test::scheduler::co_scheduler_local_check(sch_q));
            sch->schedule(test::scheduler::co_scheduler_global_check(sch_q));

            EXPECT_NE(nullptr, sch_q.pop());

            hce::scheduler* recv = (hce::scheduler*)(sch_q.pop());
            EXPECT_EQ(sch.get(), recv);
            EXPECT_NE(global_sch, recv);

            recv = (hce::scheduler*)(sch_q.pop());
            EXPECT_NE(sch.get(), recv);
            EXPECT_EQ(global_sch, recv);

            lf.reset();
            thd.join();
        } catch(const std::exception& e) {
            LOG_F(ERROR, e.what());
        }
    }
}

namespace test {
namespace scheduler {

template <typename T>
size_t join_T() {
    std::string T_name = []() -> std::string {
        std::stringstream ss;
        ss << typeid(T).name();
        return ss.str();
    }();

    HCE_INFO_LOG("join_T<%s>",T_name.c_str());
    size_t success_count = 0;

    // join individually
    {
        std::unique_ptr<hce::scheduler::lifecycle> lf;
        auto sch = hce::scheduler::make(lf);
        std::thread thd([&]{ sch->install(); });
        std::deque<hce::awt<T>> joins;

        joins.push_back(sch->join(
            co_return_T<T>(test::init<T>(3))));
        joins.push_back(sch->join(
            co_return_T<T>(test::init<T>(2))));
        joins.push_back(sch->join(
            co_return_T<T>(test::init<T>(1))));

        try {
            T result = joins.front();
            joins.pop_front();
            EXPECT_EQ((T)test::init<T>(3), result);
            result = joins.front();
            joins.pop_front();
            EXPECT_EQ((T)test::init<T>(2), result);
            result = joins.front();
            joins.pop_front();
            EXPECT_EQ((T)test::init<T>(1), result);

            lf.reset();
            thd.join();
            ++success_count;
        } catch(const std::exception& e) {
            LOG_F(ERROR, e.what());
        }
    } 

    // join individually in reverse order
    {
        std::unique_ptr<hce::scheduler::lifecycle> lf;
        auto sch = hce::scheduler::make(lf);
        std::thread thd([&]{ sch->install(); });
        std::deque<hce::awt<T>> joins;

        joins.push_back(sch->join(co_return_T<T>(test::init<T>(3))));
        joins.push_back(sch->join(co_return_T<T>(test::init<T>(2))));
        joins.push_back(sch->join(co_return_T<T>(test::init<T>(1))));

        try {
            T result = joins.back();
            joins.pop_back();
            EXPECT_EQ((T)test::init<T>(1), result);
            result = joins.back();
            joins.pop_back();
            EXPECT_EQ((T)test::init<T>(2), result);
            result = joins.back();
            joins.pop_back();
            EXPECT_EQ((T)test::init<T>(3), result);

            lf.reset();
            thd.join();
            ++success_count;
        } catch(const std::exception& e) {
            LOG_F(ERROR, e.what());
        }
    }

    // join void
    {
        std::unique_ptr<hce::scheduler::lifecycle> lf;
        auto sch = hce::scheduler::make(lf);
        std::thread thd([&]{ sch->install(); });
        std::deque<hce::awt<void>> joins;

        joins.push_back(sch->join(co_void()));
        joins.push_back(sch->join(co_void()));
        joins.push_back(sch->join(co_void()));

        try {
            joins.pop_front();
            joins.pop_front();
            joins.pop_front();

            lf.reset();
            thd.join();
            ++success_count;
        } catch(const std::exception& e) {
            LOG_F(ERROR, e.what());
        }
    }

    return success_count;
}

}
}

TEST(scheduler, join) {
    const size_t expected = 3;
    EXPECT_EQ(expected, test::scheduler::join_T<int>());
    EXPECT_EQ(expected, test::scheduler::join_T<unsigned int>());
    EXPECT_EQ(expected, test::scheduler::join_T<size_t>());
    EXPECT_EQ(expected, test::scheduler::join_T<float>());
    EXPECT_EQ(expected, test::scheduler::join_T<double>());
    EXPECT_EQ(expected, test::scheduler::join_T<char>());
    EXPECT_EQ(expected, test::scheduler::join_T<void*>());
    EXPECT_EQ(expected, test::scheduler::join_T<std::string>());
    EXPECT_EQ(expected, test::scheduler::join_T<test::CustomObject>());
}

namespace test {
namespace scheduler {

template <typename T>
size_t scope_T() {
    std::string T_name = []() -> std::string {
        std::stringstream ss;
        ss << typeid(T).name();
        return ss.str();
    }();

    HCE_INFO_LOG("scope_T<%s>",T_name.c_str());
    size_t success_count = 0;

    // scope void individually
    {
        std::unique_ptr<hce::scheduler::lifecycle> lf;
        auto sch = hce::scheduler::make(lf);
        std::thread thd([&]{ sch->install(); });
        std::deque<hce::awt<void>> scopes;

        scopes.push_back(sch->scope(co_void()));
        scopes.push_back(sch->scope(co_void()));
        scopes.push_back(sch->scope(co_void()));

        try {
            scopes.front();
            scopes.pop_front();
            scopes.front();
            scopes.pop_front();
            scopes.front();
            scopes.pop_front();

            lf.reset();
            thd.join();
            ++success_count;
        } catch(const std::exception& e) {
            LOG_F(ERROR, e.what());
        }
    } 

    // scope void ran successfully
    {
        test::queue<T> q;
        std::unique_ptr<hce::scheduler::lifecycle> lf;
        auto sch = hce::scheduler::make(lf);
        std::thread thd([&]{ sch->install(); });
        std::deque<hce::awt<void>> scopes;

        scopes.push_back(sch->scope(co_push_T<T>(q,test::init<T>(3))));
        scopes.push_back(sch->scope(co_push_T<T>(q,test::init<T>(2))));
        scopes.push_back(sch->scope(co_push_T<T>(q,test::init<T>(1))));

        try {
            scopes.front();
            scopes.pop_front();
            scopes.front();
            scopes.pop_front();
            scopes.front();
            scopes.pop_front();

            EXPECT_EQ(3,q.size());
            EXPECT_EQ((T)test::init<T>(3),q.pop());
            EXPECT_EQ((T)test::init<T>(2),q.pop());
            EXPECT_EQ((T)test::init<T>(1),q.pop());

            lf.reset();
            thd.join();
            ++success_count;
        } catch(const std::exception& e) {
            LOG_F(ERROR, e.what());
        }
    } 

    // scope void group
    {
        std::unique_ptr<hce::scheduler::lifecycle> lf;
        auto sch = hce::scheduler::make(lf);
        std::thread thd([&]{ sch->install(); });
        std::deque<hce::awt<void>> scopes;

        scopes.push_back(sch->scope(
            co_void(),
            co_void(),
            co_void()));

        try {
            scopes.front();
            scopes.pop_front();

            lf.reset();
            thd.join();
            ++success_count;
        } catch(const std::exception& e) {
            LOG_F(ERROR, e.what());
        }
    } 

    // scope void group ran successfully
    {
        test::queue<T> q;
        std::unique_ptr<hce::scheduler::lifecycle> lf;
        auto sch = hce::scheduler::make(lf);
        std::thread thd([&]{ sch->install(); });
        std::deque<hce::awt<void>> scopes;

        scopes.push_back(sch->scope(
            co_push_T<T>(q,test::init<T>(3)),
            co_push_T<T>(q,test::init<T>(2)),
            co_push_T<T>(q,test::init<T>(1))));

        try {
            scopes.front();
            scopes.pop_front();

            EXPECT_EQ(3,q.size());
            EXPECT_EQ((T)test::init<T>(3),q.pop());
            EXPECT_EQ((T)test::init<T>(2),q.pop());
            EXPECT_EQ((T)test::init<T>(1),q.pop());

            lf.reset();
            thd.join();
            ++success_count;
        } catch(const std::exception& e) {
            LOG_F(ERROR, e.what());
        }
    } 

    // scope void mixed
    {
        std::unique_ptr<hce::scheduler::lifecycle> lf;
        auto sch = hce::scheduler::make(lf);
        std::thread thd([&]{ sch->install(); });
        std::deque<hce::awt<void>> scopes;

        scopes.push_back(sch->scope(
            co_void()));
        scopes.push_back(sch->scope(
            co_void(),
            co_void()));

        try {
            scopes.front();
            scopes.pop_front();
            scopes.front();
            scopes.pop_front();

            lf.reset();
            thd.join();
            ++success_count;
        } catch(const std::exception& e) {
            LOG_F(ERROR, e.what());
        }
    } 

    // scope void mixed ran successfully
    {
        test::queue<T> q;
        std::unique_ptr<hce::scheduler::lifecycle> lf;
        auto sch = hce::scheduler::make(lf);
        std::thread thd([&]{ sch->install(); });
        std::deque<hce::awt<void>> scopes;

        scopes.push_back(sch->scope(co_push_T<T>(q,test::init<T>(3))));
        scopes.push_back(sch->scope(
            co_push_T<T>(q,test::init<T>(2)),
            co_push_T<T>(q,test::init<T>(1))));

        try {
            scopes.front();
            scopes.pop_front();
            scopes.front();
            scopes.pop_front();

            EXPECT_EQ(3,q.size());
            EXPECT_EQ((T)test::init<T>(3),q.pop());
            EXPECT_EQ((T)test::init<T>(2),q.pop());
            EXPECT_EQ((T)test::init<T>(1),q.pop());

            lf.reset();
            thd.join();
            ++success_count;
        } catch(const std::exception& e) {
            LOG_F(ERROR, e.what());
        }
    } 

    // scope T individually
    {
        std::unique_ptr<hce::scheduler::lifecycle> lf;
        auto sch = hce::scheduler::make(lf);
        std::thread thd([&]{ sch->install(); });
        std::deque<hce::awt<void>> scopes;

        scopes.push_back(sch->scope(co_return_T<T>(test::init<T>(3))));
        scopes.push_back(sch->scope(co_return_T<T>(test::init<T>(2))));
        scopes.push_back(sch->scope(co_return_T<T>(test::init<T>(1))));

        try {
            scopes.front();
            scopes.pop_front();
            scopes.front();
            scopes.pop_front();
            scopes.front();
            scopes.pop_front();

            lf.reset();
            thd.join();
            ++success_count;
        } catch(const std::exception& e) {
            LOG_F(ERROR, e.what());
        }
    } 

    // scope T individually ran successfully
    {
        test::queue<T> q;
        std::unique_ptr<hce::scheduler::lifecycle> lf;
        auto sch = hce::scheduler::make(lf);
        std::thread thd([&]{ sch->install(); });
        std::deque<hce::awt<void>> scopes;

        scopes.push_back(sch->scope(co_push_T_return_T<T>(q,test::init<T>(3))));
        scopes.push_back(sch->scope(co_push_T_return_T<T>(q,test::init<T>(2))));
        scopes.push_back(sch->scope(co_push_T_return_T<T>(q,test::init<T>(1))));

        try {
            scopes.front();
            scopes.pop_front();
            scopes.front();
            scopes.pop_front();
            scopes.front();
            scopes.pop_front();

            EXPECT_EQ(3,q.size());
            EXPECT_EQ((T)test::init<T>(3),q.pop());
            EXPECT_EQ((T)test::init<T>(2),q.pop());
            EXPECT_EQ((T)test::init<T>(1),q.pop());

            lf.reset();
            thd.join();
            ++success_count;
        } catch(const std::exception& e) {
            LOG_F(ERROR, e.what());
        }
    } 

    // scope T group
    {
        std::unique_ptr<hce::scheduler::lifecycle> lf;
        auto sch = hce::scheduler::make(lf);
        std::thread thd([&]{ sch->install(); });
        std::deque<hce::awt<void>> scopes;

        scopes.push_back(sch->scope(
            co_return_T<T>(test::init<T>(3)),
            co_return_T<T>(test::init<T>(2)),
            co_return_T<T>(test::init<T>(1))));

        try {
            scopes.front();
            scopes.pop_front();

            lf.reset();
            thd.join();
            ++success_count;
        } catch(const std::exception& e) {
            LOG_F(ERROR, e.what());
        }
    } 

    // scope T group ran successfully
    {
        test::queue<T> q;
        std::unique_ptr<hce::scheduler::lifecycle> lf;
        auto sch = hce::scheduler::make(lf);
        std::thread thd([&]{ sch->install(); });
        std::deque<hce::awt<void>> scopes;

        scopes.push_back(sch->scope(
            co_push_T_return_T<T>(q,test::init<T>(3)),
            co_push_T_return_T<T>(q,test::init<T>(2)),
            co_push_T_return_T<T>(q,test::init<T>(1))));

        try {
            scopes.front();
            scopes.pop_front();

            EXPECT_EQ(3,q.size());
            EXPECT_EQ((T)test::init<T>(3),q.pop());
            EXPECT_EQ((T)test::init<T>(2),q.pop());
            EXPECT_EQ((T)test::init<T>(1),q.pop());

            lf.reset();
            thd.join();
            ++success_count;
        } catch(const std::exception& e) {
            LOG_F(ERROR, e.what());
        }
    } 

    // scope T mixed
    {
        std::unique_ptr<hce::scheduler::lifecycle> lf;
        auto sch = hce::scheduler::make(lf);
        std::thread thd([&]{ sch->install(); });
        std::deque<hce::awt<void>> scopes;

        scopes.push_back(sch->scope(co_return_T<T>(test::init<T>(3))));
        scopes.push_back(sch->scope(
            co_return_T<T>(test::init<T>(2)),
            co_return_T<T>(test::init<T>(1))));

        try {
            scopes.front();
            scopes.pop_front();
            scopes.front();
            scopes.pop_front();

            lf.reset();
            thd.join();
            ++success_count;
        } catch(const std::exception& e) {
            LOG_F(ERROR, e.what());
        }
    } 

    // scope T mixed ran successfully
    {
        test::queue<T> q;
        std::unique_ptr<hce::scheduler::lifecycle> lf;
        auto sch = hce::scheduler::make(lf);
        std::thread thd([&]{ sch->install(); });
        std::deque<hce::awt<void>> scopes;

        scopes.push_back(sch->scope(co_push_T_return_T<T>(q,test::init<T>(3))));
        scopes.push_back(sch->scope(
            co_push_T_return_T<T>(q,test::init<T>(2)),
            co_push_T_return_T<T>(q,test::init<T>(1))));

        try {
            scopes.front();
            scopes.pop_front();
            scopes.front();
            scopes.pop_front();

            EXPECT_EQ(3,q.size());
            EXPECT_EQ((T)test::init<T>(3),q.pop());
            EXPECT_EQ((T)test::init<T>(2),q.pop());
            EXPECT_EQ((T)test::init<T>(1),q.pop());

            lf.reset();
            thd.join();
            ++success_count;
        } catch(const std::exception& e) {
            LOG_F(ERROR, e.what());
        }
    } 

    return success_count;
}

}
}

TEST(scheduler, scope) {
    const size_t expected = 12;
    EXPECT_EQ(expected, test::scheduler::scope_T<int>());
    EXPECT_EQ(expected, test::scheduler::scope_T<unsigned int>());
    EXPECT_EQ(expected, test::scheduler::scope_T<size_t>());
    EXPECT_EQ(expected, test::scheduler::scope_T<float>());
    EXPECT_EQ(expected, test::scheduler::scope_T<double>());
    EXPECT_EQ(expected, test::scheduler::scope_T<char>());
    EXPECT_EQ(expected, test::scheduler::scope_T<void*>());
    EXPECT_EQ(expected, test::scheduler::scope_T<std::string>());
    EXPECT_EQ(expected, test::scheduler::scope_T<test::CustomObject>());
}

namespace test {
namespace scheduler {

hce::chrono::duration absolute_difference(
    const hce::chrono::duration& d0, 
    const hce::chrono::duration& d1) {
    return d0 > d1 ? (d0 - d1) : (d1 - d0);
}

template <typename... As>
hce::co<bool> co_start(
        test::queue<hce::id>& q,
        As&&... as) {
    hce::id i;
    auto awt = hce::scheduler::local().start(i,as...);
    q.push(i);
    co_return co_await std::move(awt);
}

template <typename... As>
size_t scheduler_start_As(As&&... as) {
    const size_t upper_bound_overslept_milli_ticks = 50;
    size_t success_count = 0;

    std::unique_ptr<hce::scheduler::lifecycle> lf;
    auto sch = hce::scheduler::make(lf);
    std::thread thd([&]{ sch->install(); });

    // thread timer timeout
    {
        auto now = hce::chrono::now();
        auto requested_sleep_ticks = 
                hce::chrono::duration(as...).
                    to_count<hce::chrono::milliseconds>();
        hce::chrono::time_point target_timeout(hce::chrono::duration(as...) + now);
        hce::id i;
        EXPECT_TRUE((bool)sch->start(i, as...));

        auto done = hce::chrono::now();
        auto slept_ticks = absolute_difference(done,now).to_count<hce::chrono::milliseconds>();
        auto overslept_ticks = absolute_difference(target_timeout, done).to_count<hce::chrono::milliseconds>();

        // ensure we slept at least the correct amount of time
        EXPECT_GE(slept_ticks, requested_sleep_ticks); 

        // ensure we didn't sleep past the upper bound
        EXPECT_LT(overslept_ticks, upper_bound_overslept_milli_ticks);

        ++success_count;
    }

    // thread sleep through timer timeout
    {
        auto now = hce::chrono::now();
        auto requested_sleep_ticks = 
                hce::chrono::duration(as...).
                    to_count<hce::chrono::milliseconds>();
        hce::chrono::time_point target_timeout(hce::chrono::duration(as...) + now);
        hce::id i;
        auto awt = sch->start(i, as...);

        // ensure we sleep for the entire normal 
        std::this_thread::sleep_for(hce::chrono::duration(as...));

        // the awaitable should return immediately
        EXPECT_TRUE((bool)awt);

        auto done = hce::chrono::now();
        auto slept_ticks = absolute_difference(done,now).to_count<hce::chrono::milliseconds>();
        auto overslept_ticks = absolute_difference(target_timeout, done).to_count<hce::chrono::milliseconds>();

        // ensure we slept at least the correct amount of time
        EXPECT_GE(slept_ticks, requested_sleep_ticks); 

        // ensure we didn't sleep past the upper bound
        EXPECT_LT(overslept_ticks, upper_bound_overslept_milli_ticks);

        ++success_count;
    }

    // coroutine timer timeout
    {
        test::queue<hce::id> q;
        auto now = hce::chrono::now();
        auto requested_sleep_ticks = 
                hce::chrono::duration(as...).
                    to_count<hce::chrono::milliseconds>();
        hce::chrono::time_point target_timeout(hce::chrono::duration(as...) + now);
        EXPECT_TRUE((bool)sch->join(co_start(q, as...)));

        auto done = hce::chrono::now();
        auto slept_ticks = absolute_difference(done,now).to_count<hce::chrono::milliseconds>();
        auto overslept_ticks = absolute_difference(target_timeout, done).to_count<hce::chrono::milliseconds>();

        // ensure we slept at least the correct amount of time
        EXPECT_GE(slept_ticks, requested_sleep_ticks); 

        // ensure we didn't sleep past the upper bound
        EXPECT_LT(overslept_ticks, upper_bound_overslept_milli_ticks);

        ++success_count;
    }

    lf.reset();
    thd.join();

    return success_count; 
}

}
}

TEST(scheduler, start) {
    EXPECT_EQ(3,test::scheduler::scheduler_start_As(hce::chrono::milliseconds(50)));
    EXPECT_EQ(3,test::scheduler::scheduler_start_As(hce::chrono::microseconds(5000)));
    EXPECT_EQ(3,test::scheduler::scheduler_start_As(hce::chrono::nanoseconds(500000)));
    EXPECT_EQ(3,test::scheduler::scheduler_start_As(hce::chrono::duration(hce::chrono::milliseconds(50))));
    EXPECT_EQ(3,test::scheduler::scheduler_start_As(hce::chrono::duration(hce::chrono::microseconds(5000))));
    EXPECT_EQ(3,test::scheduler::scheduler_start_As(hce::chrono::duration(hce::chrono::nanoseconds(500000))));
    EXPECT_EQ(3,test::scheduler::scheduler_start_As(hce::chrono::time_point(hce::chrono::duration(hce::chrono::milliseconds(50)))));
    EXPECT_EQ(3,test::scheduler::scheduler_start_As(hce::chrono::time_point(hce::chrono::duration(hce::chrono::microseconds(5000)))));
    EXPECT_EQ(3,test::scheduler::scheduler_start_As(hce::chrono::time_point(hce::chrono::duration(hce::chrono::nanoseconds(500000)))));
}

namespace test {
namespace scheduler {

template <typename... As>
size_t scheduler_cancel_As(As&&... as) {
    const size_t upper_bound_overslept_milli_ticks = 50;
    size_t success_count = 0;

    std::unique_ptr<hce::scheduler::lifecycle> lf;
    auto sch = hce::scheduler::make(lf);
    std::thread thd([&]{ sch->install(); });

    // thread timer cancel
    {
        test::queue<hce::id> q;

        std::thread sleeping_thd([&]{
            hce::id i;
            auto now = hce::chrono::now();
            auto requested_sleep_ticks = 
                    hce::chrono::duration(as...).
                        to_count<hce::chrono::milliseconds>();
            hce::chrono::time_point target_timeout(hce::chrono::duration(as...) + now);
            auto awt = sch->start(i, as...);
            q.push(i);
            EXPECT_FALSE((bool)awt);

            auto done = hce::chrono::now();
            auto slept_ticks = absolute_difference(done,now).to_count<hce::chrono::milliseconds>();
            auto overslept_ticks = absolute_difference(target_timeout, done).to_count<hce::chrono::milliseconds>();

            // ensure we slept at least the correct amount of time
            EXPECT_GE(slept_ticks, requested_sleep_ticks); 

            // ensure we didn't sleep past the upper bound
            EXPECT_LT(overslept_ticks, upper_bound_overslept_milli_ticks);
        });

        hce::id i = q.pop();
        EXPECT_TRUE(sch->cancel(i));
        sleeping_thd.join();

        ++success_count;
    }

    // coroutine timer cancel
    {
        test::queue<hce::id> q;
        auto now = hce::chrono::now();
        auto requested_sleep_ticks = 
            hce::chrono::duration(as...).
                to_count<hce::chrono::milliseconds>();
        hce::chrono::time_point target_timeout(hce::chrono::duration(as...) + now);
        auto awt = sch->join(co_start(q, as...));
        hce::id i = q.pop();
        sch->cancel(i);

        EXPECT_TRUE((bool)std::move(awt));

        auto done = hce::chrono::now();
        auto slept_ticks = absolute_difference(done,now).to_count<hce::chrono::milliseconds>();
        auto overslept_ticks = absolute_difference(target_timeout, done).to_count<hce::chrono::milliseconds>();

        // ensure we slept at least the correct amount of time
        EXPECT_GE(slept_ticks, requested_sleep_ticks); 

        // ensure we didn't sleep past the upper bound
        EXPECT_LT(overslept_ticks, upper_bound_overslept_milli_ticks);

        ++success_count;
    }

    lf.reset();
    thd.join();

    return success_count; 
}

}
}

TEST(scheduler, cancel) {
    EXPECT_EQ(2,test::scheduler::scheduler_cancel_As(hce::chrono::milliseconds(50)));
    EXPECT_EQ(3,test::scheduler::scheduler_cancel_As(hce::chrono::microseconds(5000)));
    EXPECT_EQ(3,test::scheduler::scheduler_cancel_As(hce::chrono::nanoseconds(500000)));
    EXPECT_EQ(3,test::scheduler::scheduler_cancel_As(hce::chrono::duration(hce::chrono::milliseconds(50))));
    EXPECT_EQ(3,test::scheduler::scheduler_cancel_As(hce::chrono::duration(hce::chrono::microseconds(5000))));
    EXPECT_EQ(3,test::scheduler::scheduler_cancel_As(hce::chrono::duration(hce::chrono::nanoseconds(500000))));
    EXPECT_EQ(3,test::scheduler::scheduler_cancel_As(hce::chrono::time_point(hce::chrono::duration(hce::chrono::milliseconds(50)))));
    EXPECT_EQ(3,test::scheduler::scheduler_cancel_As(hce::chrono::time_point(hce::chrono::duration(hce::chrono::microseconds(5000)))));
    EXPECT_EQ(3,test::scheduler::scheduler_cancel_As(hce::chrono::time_point(hce::chrono::duration(hce::chrono::nanoseconds(500000)))));
}
