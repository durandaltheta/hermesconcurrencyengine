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

namespace test {
namespace scheduler {

/*
 Test only replacement for something like a channel. Synchronizes sends and 
 receives between a thread and a thread or a thread and a coroutine
 */
template <typename T>
struct queue {
    template <typename TSHADOW>
    void push(TSHADOW&& t) {
        {
            std::lock_guard<hce::spinlock> lk(slk);
            vals.push_back(std::forward<TSHADOW>(t));
        }
        cv.notify_one();
    }

    T pop() {
        std::unique_lock<hce::spinlock> lk(slk);
        while(!vals.size()) {
            cv.wait(lk);
        }

        T res = std::move(vals.front());
        vals.pop_front();
        return res;
    }

    size_t size() {
        std::lock_guard<hce::spinlock> lk(slk);
        return vals.size();
    }

private:
    hce::spinlock slk;
    std::condition_variable_any cv;
    std::deque<T> vals;
};

inline hce::co<void> co_void() { co_return; }

template <typename T>
hce::co<void> co_push_T(queue<T>& q, T t) {
    q.push(t);
    co_return;
}

template <typename T>
inline hce::co<T> co_return_T(T t) {
    co_return t;
}

template <typename T>
hce::co<T> co_push_T_return_T(queue<T>& q, T t) {
    q.push(t);
    co_return t;
}

inline hce::co<void> co_scheduler_in_check(queue<void*>& q) { 
    q.push(hce::scheduler::in() ? (void*)1 : (void*)0);
    co_return;
}

inline hce::co<void> co_scheduler_local_check(queue<void*>& q) { 
    q.push(&(hce::scheduler::local()));
    co_return;
}

inline hce::co<void> co_scheduler_global_check(queue<void*>& q) { 
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
        test::scheduler::queue<hce::scheduler::state> state_q;
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
        test::scheduler::queue<hce::scheduler::state> state_q;
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

struct CustomObject {
    CustomObject() : i_(0) {}
    CustomObject(const CustomObject&) = default;
    CustomObject(CustomObject&&) = default;

    CustomObject(int i) : i_(i) {}

    CustomObject& operator=(const CustomObject&) = default;
    CustomObject& operator=(CustomObject&&) = default;

    inline bool operator==(const CustomObject& rhs) const { 
        return i_ == rhs.i_; 
    }

    inline bool operator!=(const CustomObject& rhs) const { 
        return !(*this == rhs); 
    }

private:
    int i_;
};

// Provides a standard initialization API that enables template specialization
template <typename T>
struct init {
    // initialize value 
    template <typename... As>
    init(As&&... as) : t_(std::forward<As>(as)...) { }

    // trivial conversion
    inline operator T() { return std::move(t_); }

private:
    T t_;
};

// void* initialization specialization
template <>
struct init<void*> {
    template <typename... As>
    init(As&&... as) : t_(((void*)(size_t)as)...) { }

    inline operator void*() { return std::move(t_); }

private:
    void* t_;
};

// std::string initialization specialization
template <>
struct init<std::string> {
    template <typename... As>
    init(As&&... as) : t_(std::to_string(std::forward<As>(as)...)) { }

    inline operator std::string() { return std::move(t_); }

private:
    std::string t_;
};

template <typename T>
size_t schedule_T() {
    std::string T_name = []() -> std::string {
        std::stringstream ss;
        ss << typeid(T).name();
        return ss.str();
    }();

    HCE_INFO_LOG("schedule_T<%s>",T_name.c_str());

    size_t success_count = 0;

    // schedule individually
    {
        test::scheduler::queue<T> q;
        std::unique_ptr<hce::scheduler::lifecycle> lf;
        auto sch = hce::scheduler::make(lf);
        std::thread thd([&]{ sch->install(); });

        sch->schedule(test::scheduler::co_push_T<T>(q,init<T>(3)));
        sch->schedule(test::scheduler::co_push_T<T>(q,init<T>(2)));
        sch->schedule(test::scheduler::co_push_T<T>(q,init<T>(1)));

        try {
            EXPECT_EQ((T)init<T>(3), q.pop());
            EXPECT_EQ((T)init<T>(2), q.pop());
            EXPECT_EQ((T)init<T>(1), q.pop());

            lf.reset();
            thd.join();
            ++success_count;
        } catch(const std::exception& e) {
            LOG_F(ERROR, e.what());
        }
    }

    // schedule group
    {
        test::scheduler::queue<T> q;
        std::unique_ptr<hce::scheduler::lifecycle> lf;
        auto sch = hce::scheduler::make(lf);
        std::thread thd([&]{ sch->install(); });

        sch->schedule(test::scheduler::co_push_T<T>(q,init<T>(3)),
                      test::scheduler::co_push_T<T>(q,init<T>(2)),
                      test::scheduler::co_push_T<T>(q,init<T>(1)));

        try {
            EXPECT_EQ((T)init<T>(3), q.pop());
            EXPECT_EQ((T)init<T>(2), q.pop());
            EXPECT_EQ((T)init<T>(1), q.pop());

            lf.reset();
            thd.join();
            ++success_count;
        } catch(const std::exception& e) {
            LOG_F(ERROR, e.what());
        }
    }

    // schedule group of base hce::coroutines
    {
        test::scheduler::queue<T> q;
        std::unique_ptr<hce::scheduler::lifecycle> lf;
        auto sch = hce::scheduler::make(lf);
        std::thread thd([&]{ sch->install(); });

        sch->schedule(hce::coroutine(test::scheduler::co_push_T<T>(q,init<T>(3))),
                      hce::coroutine(test::scheduler::co_push_T<T>(q,init<T>(2))),
                      hce::coroutine(test::scheduler::co_push_T<T>(q,init<T>(1))));

        try {
            EXPECT_EQ((T)init<T>(3), q.pop());
            EXPECT_EQ((T)init<T>(2), q.pop());
            EXPECT_EQ((T)init<T>(1), q.pop());

            lf.reset();
            thd.join();
            ++success_count;
        } catch(const std::exception& e) {
            LOG_F(ERROR, e.what());
        }
    }

    // schedule group of different coroutine signatures
    {
        test::scheduler::queue<T> q;
        std::unique_ptr<hce::scheduler::lifecycle> lf;
        auto sch = hce::scheduler::make(lf);
        std::thread thd([&]{ sch->install(); });

        sch->schedule(test::scheduler::co_push_T<T>(q,init<T>(3)),
                      hce::coroutine(test::scheduler::co_push_T<T>(q,init<T>(2))),
                      test::scheduler::co_push_T_return_T<T>(q,init<T>(1)));

        try {
            EXPECT_EQ((T)init<T>(3), q.pop());
            EXPECT_EQ((T)init<T>(2), q.pop());
            EXPECT_EQ((T)init<T>(1), q.pop());

            lf.reset();
            thd.join();
            ++success_count;
        } catch(const std::exception& e) {
            LOG_F(ERROR, e.what());
        }
    }

    // schedule group and single
    {
        test::scheduler::queue<T> q;
        std::unique_ptr<hce::scheduler::lifecycle> lf;
        auto sch = hce::scheduler::make(lf);
        std::thread thd([&]{ sch->install(); });

        sch->schedule(test::scheduler::co_push_T<T>(q,init<T>(3)),
                      test::scheduler::co_push_T<T>(q,init<T>(2)));
        sch->schedule(test::scheduler::co_push_T<T>(q,init<T>(1)));

        try {
            EXPECT_EQ((T)init<T>(3), q.pop());
            EXPECT_EQ((T)init<T>(2), q.pop());
            EXPECT_EQ((T)init<T>(1), q.pop());

            lf.reset();
            thd.join();
            ++success_count;
        } catch(const std::exception& e) {
            LOG_F(ERROR, e.what());
        }
    }

    // schedule in a vector
    {
        test::scheduler::queue<T> q;
        std::unique_ptr<hce::scheduler::lifecycle> lf;
        auto sch = hce::scheduler::make(lf);
        std::thread thd([&]{ sch->install(); });

        std::vector<hce::co<void>> cos;
        cos.push_back(test::scheduler::co_push_T<T>(q,init<T>(3)));
        cos.push_back(test::scheduler::co_push_T<T>(q,init<T>(2)));
        cos.push_back(test::scheduler::co_push_T<T>(q,init<T>(1)));

        try {
            sch->schedule(std::move(cos));

            EXPECT_EQ((T)init<T>(3), q.pop());
            EXPECT_EQ((T)init<T>(2), q.pop());
            EXPECT_EQ((T)init<T>(1), q.pop());

            lf.reset();
            thd.join();
            ++success_count;
        } catch(const std::exception& e) {
            LOG_F(ERROR, e.what());
        }
    }

    // schedule in a list
    {
        test::scheduler::queue<T> q;
        std::unique_ptr<hce::scheduler::lifecycle> lf;
        auto sch = hce::scheduler::make(lf);
        std::thread thd([&]{ sch->install(); });

        std::list<hce::co<void>> cos;
        cos.push_back(test::scheduler::co_push_T<T>(q,init<T>(3)));
        cos.push_back(test::scheduler::co_push_T<T>(q,init<T>(2)));
        cos.push_back(test::scheduler::co_push_T<T>(q,init<T>(1)));

        try {
            sch->schedule(std::move(cos));

            EXPECT_EQ((T)init<T>(3), q.pop());
            EXPECT_EQ((T)init<T>(2), q.pop());
            EXPECT_EQ((T)init<T>(1), q.pop());

            lf.reset();
            thd.join();
            ++success_count;
        } catch(const std::exception& e) {
            LOG_F(ERROR, e.what());
        }
    }

    // schedule in a forward_list
    {
        test::scheduler::queue<T> q;
        std::unique_ptr<hce::scheduler::lifecycle> lf;
        auto sch = hce::scheduler::make(lf);
        std::thread thd([&]{ sch->install(); });

        std::forward_list<hce::co<void>> cos;
        cos.push_front(test::scheduler::co_push_T<T>(q,init<T>(3)));
        cos.push_front(test::scheduler::co_push_T<T>(q,init<T>(2)));
        cos.push_front(test::scheduler::co_push_T<T>(q,init<T>(1)));

        try {
            sch->schedule(std::move(cos));

            EXPECT_EQ((T)init<T>(1), q.pop());
            EXPECT_EQ((T)init<T>(2), q.pop());
            EXPECT_EQ((T)init<T>(3), q.pop());

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

TEST(scheduler, schedule) {
    // the count of schedule subtests we expect to complete without throwing 
    // exceptions
    const size_t expected = 8;
    EXPECT_EQ(expected, test::schedule_T<int>());
    EXPECT_EQ(expected, test::schedule_T<unsigned int>());
    EXPECT_EQ(expected, test::schedule_T<size_t>());
    EXPECT_EQ(expected, test::schedule_T<float>());
    EXPECT_EQ(expected, test::schedule_T<double>());
    EXPECT_EQ(expected, test::schedule_T<char>());
    EXPECT_EQ(expected, test::schedule_T<void*>());
    EXPECT_EQ(expected, test::schedule_T<std::string>());
    EXPECT_EQ(expected, test::schedule_T<test::CustomObject>());
}

TEST(scheduler, schedule_and_thread_locals) {
    {
        test::scheduler::queue<void*> sch_q;
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
            test::scheduler::co_return_T<T>(test::init<T>(3))));
        joins.push_back(sch->join(
            test::scheduler::co_return_T<T>(test::init<T>(2))));
        joins.push_back(sch->join(
            test::scheduler::co_return_T<T>(test::init<T>(1))));

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

        joins.push_back(sch->join(
            test::scheduler::co_return_T<T>(test::init<T>(3))));
        joins.push_back(sch->join(
            test::scheduler::co_return_T<T>(test::init<T>(2))));
        joins.push_back(sch->join(
            test::scheduler::co_return_T<T>(test::init<T>(1))));

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

        joins.push_back(sch->join(test::scheduler::co_void()));
        joins.push_back(sch->join(test::scheduler::co_void()));
        joins.push_back(sch->join(test::scheduler::co_void()));

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

TEST(scheduler, join) {
    const size_t expected = 3;
    EXPECT_EQ(expected, test::join_T<int>());
    EXPECT_EQ(expected, test::join_T<unsigned int>());
    EXPECT_EQ(expected, test::join_T<size_t>());
    EXPECT_EQ(expected, test::join_T<float>());
    EXPECT_EQ(expected, test::join_T<double>());
    EXPECT_EQ(expected, test::join_T<char>());
    EXPECT_EQ(expected, test::join_T<void*>());
    EXPECT_EQ(expected, test::join_T<std::string>());
    EXPECT_EQ(expected, test::join_T<test::CustomObject>());
}

namespace test {

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

        scopes.push_back(sch->scope(
            test::scheduler::co_void()));
        scopes.push_back(sch->scope(
            test::scheduler::co_void()));
        scopes.push_back(sch->scope(
            test::scheduler::co_void()));

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
        test::scheduler::queue<T> q;
        std::unique_ptr<hce::scheduler::lifecycle> lf;
        auto sch = hce::scheduler::make(lf);
        std::thread thd([&]{ sch->install(); });
        std::deque<hce::awt<void>> scopes;

        scopes.push_back(sch->scope(
            test::scheduler::co_push_T<T>(q,test::init<T>(3))));
        scopes.push_back(sch->scope(
            test::scheduler::co_push_T<T>(q,test::init<T>(2))));
        scopes.push_back(sch->scope(
            test::scheduler::co_push_T<T>(q,test::init<T>(1))));

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
            test::scheduler::co_void(),
            test::scheduler::co_void(),
            test::scheduler::co_void()));

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
        test::scheduler::queue<T> q;
        std::unique_ptr<hce::scheduler::lifecycle> lf;
        auto sch = hce::scheduler::make(lf);
        std::thread thd([&]{ sch->install(); });
        std::deque<hce::awt<void>> scopes;

        scopes.push_back(sch->scope(
            test::scheduler::co_push_T<T>(q,test::init<T>(3)),
            test::scheduler::co_push_T<T>(q,test::init<T>(2)),
            test::scheduler::co_push_T<T>(q,test::init<T>(1))));

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
            test::scheduler::co_void()));
        scopes.push_back(sch->scope(
            test::scheduler::co_void(),
            test::scheduler::co_void()));

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
        test::scheduler::queue<T> q;
        std::unique_ptr<hce::scheduler::lifecycle> lf;
        auto sch = hce::scheduler::make(lf);
        std::thread thd([&]{ sch->install(); });
        std::deque<hce::awt<void>> scopes;

        scopes.push_back(sch->scope(
            test::scheduler::co_push_T<T>(q,test::init<T>(3))));
        scopes.push_back(sch->scope(
            test::scheduler::co_push_T<T>(q,test::init<T>(2)),
            test::scheduler::co_push_T<T>(q,test::init<T>(1))));

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

        scopes.push_back(sch->scope(
            test::scheduler::co_return_T<T>(test::init<T>(3))));
        scopes.push_back(sch->scope(
            test::scheduler::co_return_T<T>(test::init<T>(2))));
        scopes.push_back(sch->scope(
            test::scheduler::co_return_T<T>(test::init<T>(1))));

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
        test::scheduler::queue<T> q;
        std::unique_ptr<hce::scheduler::lifecycle> lf;
        auto sch = hce::scheduler::make(lf);
        std::thread thd([&]{ sch->install(); });
        std::deque<hce::awt<void>> scopes;

        scopes.push_back(sch->scope(
            test::scheduler::co_push_T_return_T<T>(q,test::init<T>(3))));
        scopes.push_back(sch->scope(
            test::scheduler::co_push_T_return_T<T>(q,test::init<T>(2))));
        scopes.push_back(sch->scope(
            test::scheduler::co_push_T_return_T<T>(q,test::init<T>(1))));

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
            test::scheduler::co_return_T<T>(test::init<T>(3)),
            test::scheduler::co_return_T<T>(test::init<T>(2)),
            test::scheduler::co_return_T<T>(test::init<T>(1))));

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
        test::scheduler::queue<T> q;
        std::unique_ptr<hce::scheduler::lifecycle> lf;
        auto sch = hce::scheduler::make(lf);
        std::thread thd([&]{ sch->install(); });
        std::deque<hce::awt<void>> scopes;

        scopes.push_back(sch->scope(
            test::scheduler::co_push_T_return_T<T>(q,test::init<T>(3)),
            test::scheduler::co_push_T_return_T<T>(q,test::init<T>(2)),
            test::scheduler::co_push_T_return_T<T>(q,test::init<T>(1))));

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

        scopes.push_back(sch->scope(
            test::scheduler::co_return_T<T>(test::init<T>(3))));
        scopes.push_back(sch->scope(
            test::scheduler::co_return_T<T>(test::init<T>(2)),
            test::scheduler::co_return_T<T>(test::init<T>(1))));

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
        test::scheduler::queue<T> q;
        std::unique_ptr<hce::scheduler::lifecycle> lf;
        auto sch = hce::scheduler::make(lf);
        std::thread thd([&]{ sch->install(); });
        std::deque<hce::awt<void>> scopes;

        scopes.push_back(sch->scope(
            test::scheduler::co_push_T_return_T<T>(q,test::init<T>(3))));
        scopes.push_back(sch->scope(
            test::scheduler::co_push_T_return_T<T>(q,test::init<T>(2)),
            test::scheduler::co_push_T_return_T<T>(q,test::init<T>(1))));

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

TEST(scheduler, scope) {
    const size_t expected = 12;
    EXPECT_EQ(expected, test::scope_T<int>());
    EXPECT_EQ(expected, test::scope_T<unsigned int>());
    EXPECT_EQ(expected, test::scope_T<size_t>());
    EXPECT_EQ(expected, test::scope_T<float>());
    EXPECT_EQ(expected, test::scope_T<double>());
    EXPECT_EQ(expected, test::scope_T<char>());
    EXPECT_EQ(expected, test::scope_T<void*>());
    EXPECT_EQ(expected, test::scope_T<std::string>());
    EXPECT_EQ(expected, test::scope_T<test::CustomObject>());
}
