//SPDX-License-Identifier: Apache-2.0
//Author: Blayne Dennis 
#include <deque>
#include <string>

#include "scheduler.hpp"

#include <gtest/gtest.h> 
#include "test_helpers.hpp"

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
        auto lf = hce::scheduler::make();
        std::shared_ptr<hce::scheduler> sch = lf->scheduler();
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
            ++success_count;
        } catch(const std::exception& e) {
            LOG_F(ERROR, e.what());
        }
    } 

    // scope void ran successfully
    {
        test::queue<T> q;
        auto lf = hce::scheduler::make();
        std::shared_ptr<hce::scheduler> sch = lf->scheduler();
        std::deque<hce::scope> scopes;

        scopes.push_back(hce::scope(sch->schedule(co_push_T<T>(q,test::init<T>(3)))));
        scopes.push_back(hce::scope(sch->schedule(co_push_T<T>(q,test::init<T>(2)))));
        scopes.push_back(hce::scope(sch->schedule(co_push_T<T>(q,test::init<T>(1)))));

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
            ++success_count;
        } catch(const std::exception& e) {
            LOG_F(ERROR, e.what());
        }
    } 

    // scope void group
    {
        auto lf = hce::scheduler::make();
        std::shared_ptr<hce::scheduler> sch = lf->scheduler();
        std::deque<hce::scope> scopes;

        scopes.push_back(hce::scope(
            sch->schedule(co_void()),
            sch->schedule(co_void()),
            sch->schedule(co_void())));

        try {
            scopes.front();
            scopes.pop_front();

            lf.reset();
            ++success_count;
        } catch(const std::exception& e) {
            LOG_F(ERROR, e.what());
        }
    } 

    // scope void group ran successfully
    {
        test::queue<T> q;
        auto lf = hce::scheduler::make();
        std::shared_ptr<hce::scheduler> sch = lf->scheduler();
        std::deque<hce::scope>> scopes;

        scopes.push_back(hce::scope(
            sch->schedule(co_push_T<T>(q,test::init<T>(3))),
            sch->schedule(co_push_T<T>(q,test::init<T>(2))),
            sch->schedule(co_push_T<T>(q,test::init<T>(1)))));

        try {
            scopes.front();
            scopes.pop_front();

            EXPECT_EQ(3,q.size());
            EXPECT_EQ((T)test::init<T>(3),q.pop());
            EXPECT_EQ((T)test::init<T>(2),q.pop());
            EXPECT_EQ((T)test::init<T>(1),q.pop());

            lf.reset();
            ++success_count;
        } catch(const std::exception& e) {
            LOG_F(ERROR, e.what());
        }
    } 

    // scope void mixed
    {
        auto lf = hce::scheduler::make();
        std::shared_ptr<hce::scheduler> sch = lf->scheduler();
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
            ++success_count;
        } catch(const std::exception& e) {
            LOG_F(ERROR, e.what());
        }
    } 

    // scope void mixed ran successfully
    {
        test::queue<T> q;
        auto lf = hce::scheduler::make();
        std::shared_ptr<hce::scheduler> sch = lf->scheduler();
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
            ++success_count;
        } catch(const std::exception& e) {
            LOG_F(ERROR, e.what());
        }
    } 

    // scope T individually
    {
        auto lf = hce::scheduler::make();
        std::shared_ptr<hce::scheduler> sch = lf->scheduler();
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
            ++success_count;
        } catch(const std::exception& e) {
            LOG_F(ERROR, e.what());
        }
    } 

    // scope T individually ran successfully
    {
        test::queue<T> q;
        auto lf = hce::scheduler::make();
        std::shared_ptr<hce::scheduler> sch = lf->scheduler();
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
            ++success_count;
        } catch(const std::exception& e) {
            LOG_F(ERROR, e.what());
        }
    } 

    // scope T group
    {
        auto lf = hce::scheduler::make();
        std::shared_ptr<hce::scheduler> sch = lf->scheduler();
        std::deque<hce::awt<void>> scopes;

        scopes.push_back(sch->scope(
            co_return_T<T>(test::init<T>(3)),
            co_return_T<T>(test::init<T>(2)),
            co_return_T<T>(test::init<T>(1))));

        try {
            scopes.front();
            scopes.pop_front();

            lf.reset();
            ++success_count;
        } catch(const std::exception& e) {
            LOG_F(ERROR, e.what());
        }
    } 

    // scope T group ran successfully
    {
        test::queue<T> q;
        auto lf = hce::scheduler::make();
        std::shared_ptr<hce::scheduler> sch = lf->scheduler();
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
            ++success_count;
        } catch(const std::exception& e) {
            LOG_F(ERROR, e.what());
        }
    } 

    // scope T mixed
    {
        auto lf = hce::scheduler::make();
        std::shared_ptr<hce::scheduler> sch = lf->scheduler();
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
            ++success_count;
        } catch(const std::exception& e) {
            LOG_F(ERROR, e.what());
        }
    } 

    // scope T mixed ran successfully
    {
        test::queue<T> q;
        auto lf = hce::scheduler::make();
        std::shared_ptr<hce::scheduler> sch = lf->scheduler();
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
