//SPDX-License-Identifier: Apache-2.0
//Author: Blayne Dennis 
#include <deque>
#include <string>

#include "scheduler.hpp"
#include "scope.hpp"

#include <gtest/gtest.h> 
#include "test_helpers.hpp"

namespace test {
namespace scope {

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
size_t scope_T() {
    const std::string fname = hce::type::templatize<T>("scope_T");
    size_t success_count = 0;

    // block until all scopes are awaited on this thread
    auto scope_await_thread = [](std::deque<hce::scope<>>& scopes) {
        while(scopes.size()) {
            // join with the scope
            scopes.front().await();
            scopes.pop_front();
        }
    };

    // block until coroutine returns from awaiting the scopes
    auto scope_await_co = [](hce::scheduler& sch, std::deque<hce::scope<>>& scopes) {
        // lambda should be a valid coroutine
        auto co = [](std::deque<hce::scope<>>& scopes) -> hce::co<void> {
            while(scopes.size()) {
                // join with the scope
                co_await scopes.front().await();
                scopes.pop_front();
            }

            co_return;
        };

        // schedule() awaitable goes out of scope and blocks calling thread
        sch.schedule(co(scopes));
    };

    // run both the thread and coroutine variant of the same test
    auto run_test = [&](hce::scheduler& sch,
                        std::deque<hce::scope<>>& scopes,
                        std::function<void()>& setup) 
    {
        setup();
        scope_await_thread(scopes);
        setup();
        scope_await_co(sch, scopes);
        ++success_count;
    };
        

    {
        HCE_INFO_FUNCTION_BODY(fname,"scope void individually");
        auto lf = hce::scheduler::make();
        std::shared_ptr<hce::scheduler> sch = lf->get_scheduler();
        std::deque<hce::scope<>> scopes;

        std::function<void()> setup = [&]{
            scopes.push_back(hce::scope(sch->schedule(co_void())));
            scopes.push_back(hce::scope(sch->schedule(co_void())));
            scopes.push_back(hce::scope(sch->schedule(co_void())));
        };

        run_test(*sch, scopes, setup);
    } 

    {
        HCE_INFO_FUNCTION_BODY(fname,"scope void add()ed individually");
        auto lf = hce::scheduler::make();
        std::shared_ptr<hce::scheduler> sch = lf->get_scheduler();
        std::deque<hce::scope<>> scopes;

        std::function<void()> setup = [&]{
            {
                hce::scope s;
                s.add(sch->schedule(co_void()));
                scopes.push_back(std::move(s));
            }

            {
                hce::scope s;
                s.add(sch->schedule(co_void()));
                scopes.push_back(std::move(s));
            }

            {
                hce::scope s;
                s.add(sch->schedule(co_void()));
                scopes.push_back(std::move(s));
            }
        };

        run_test(*sch, scopes, setup);
    } 

    {
        HCE_INFO_FUNCTION_BODY(fname,"scope void run successfully");
        test::queue<T> q;
        auto lf = hce::scheduler::make();
        std::shared_ptr<hce::scheduler> sch = lf->get_scheduler();
        std::deque<hce::scope<>> scopes;

        std::function<void()> setup = [&]{
            scopes.push_back(hce::scope(sch->schedule(co_push_T<T>(q,test::init<T>(3)))));
            scopes.push_back(hce::scope(sch->schedule(co_push_T<T>(q,test::init<T>(2)))));
            scopes.push_back(hce::scope(sch->schedule(co_push_T<T>(q,test::init<T>(1)))));
        };

        run_test(*sch, scopes, setup);
        EXPECT_EQ(6,q.size());
        EXPECT_EQ((T)test::init<T>(3),q.pop());
        EXPECT_EQ((T)test::init<T>(2),q.pop());
        EXPECT_EQ((T)test::init<T>(1),q.pop());
        EXPECT_EQ((T)test::init<T>(3),q.pop());
        EXPECT_EQ((T)test::init<T>(2),q.pop());
        EXPECT_EQ((T)test::init<T>(1),q.pop());
    } 

    {
        HCE_INFO_FUNCTION_BODY(fname,"scope add()ed void run successfully");
        test::queue<T> q;
        auto lf = hce::scheduler::make();
        std::shared_ptr<hce::scheduler> sch = lf->get_scheduler();
        std::deque<hce::scope<>> scopes;

        std::function<void()> setup = [&]{
            {
                hce::scope s;
                s.add(sch->schedule(co_push_T<T>(q,test::init<T>(3))));
                scopes.push_back(std::move(s));
            }

            {
                hce::scope s;
                s.add(sch->schedule(co_push_T<T>(q,test::init<T>(2))));
                scopes.push_back(std::move(s));
            }

            {
                hce::scope s;
                s.add(sch->schedule(co_push_T<T>(q,test::init<T>(1))));
                scopes.push_back(std::move(s));
            }
        };

        run_test(*sch, scopes, setup);
        EXPECT_EQ(6,q.size());
        EXPECT_EQ((T)test::init<T>(3),q.pop());
        EXPECT_EQ((T)test::init<T>(2),q.pop());
        EXPECT_EQ((T)test::init<T>(1),q.pop());
        EXPECT_EQ((T)test::init<T>(3),q.pop());
        EXPECT_EQ((T)test::init<T>(2),q.pop());
        EXPECT_EQ((T)test::init<T>(1),q.pop());
    } 

    {
        HCE_INFO_FUNCTION_BODY(fname,"scope void group");
        auto lf = hce::scheduler::make();
        std::shared_ptr<hce::scheduler> sch = lf->get_scheduler();
        std::deque<hce::scope<>> scopes;

        std::function<void()> setup = [&]{
            scopes.push_back(hce::scope(
                sch->schedule(co_void()),
                sch->schedule(co_void()),
                sch->schedule(co_void())));
        };

        run_test(*sch, scopes, setup);
    } 

    {
        HCE_INFO_FUNCTION_BODY(fname,"scope void add()ed group");
        auto lf = hce::scheduler::make();
        std::shared_ptr<hce::scheduler> sch = lf->get_scheduler();
        std::deque<hce::scope<>> scopes;

        std::function<void()> setup = [&]{
            scopes.push_back(hce::scope(
                sch->schedule(co_void()),
                sch->schedule(co_void()),
                sch->schedule(co_void())));
        };

        run_test(*sch, scopes, setup);
    } 

    {
        HCE_INFO_FUNCTION_BODY(fname,"scope void group run successfully");
        test::queue<T> q;
        auto lf = hce::scheduler::make();
        std::shared_ptr<hce::scheduler> sch = lf->get_scheduler();
        std::deque<hce::scope<>> scopes;

        std::function<void()> setup = [&]{
            auto awt1 = sch->schedule(co_push_T<T>(q,test::init<T>(3)));
            auto awt2 = sch->schedule(co_push_T<T>(q,test::init<T>(2)));
            auto awt3 = sch->schedule(co_push_T<T>(q,test::init<T>(1)));

            scopes.push_back(hce::scope(
                std::move(awt1),
                std::move(awt2),
                std::move(awt3)));
        };
        
        run_test(*sch, scopes, setup);
        EXPECT_EQ(6,q.size());
        EXPECT_EQ((T)test::init<T>(3),q.pop());
        EXPECT_EQ((T)test::init<T>(2),q.pop());
        EXPECT_EQ((T)test::init<T>(1),q.pop());
        EXPECT_EQ((T)test::init<T>(3),q.pop());
        EXPECT_EQ((T)test::init<T>(2),q.pop());
        EXPECT_EQ((T)test::init<T>(1),q.pop());
    } 

    {
        HCE_INFO_FUNCTION_BODY(fname,"scope void group add()ed run successfully");
        test::queue<T> q;
        auto lf = hce::scheduler::make();
        std::shared_ptr<hce::scheduler> sch = lf->get_scheduler();
        std::deque<hce::scope<>> scopes;

        std::function<void()> setup = [&]{
            hce::scope s;
            auto awt1 = sch->schedule(co_push_T<T>(q,test::init<T>(3)));
            auto awt2 = sch->schedule(co_push_T<T>(q,test::init<T>(2)));
            auto awt3 = sch->schedule(co_push_T<T>(q,test::init<T>(1)));
            s.add(std::move(awt1), std::move(awt2), std::move(awt3));
            scopes.push_back(std::move(s));
        };
        
        run_test(*sch, scopes, setup);
        EXPECT_EQ(6,q.size());
        EXPECT_EQ((T)test::init<T>(3),q.pop());
        EXPECT_EQ((T)test::init<T>(2),q.pop());
        EXPECT_EQ((T)test::init<T>(1),q.pop());
        EXPECT_EQ((T)test::init<T>(3),q.pop());
        EXPECT_EQ((T)test::init<T>(2),q.pop());
        EXPECT_EQ((T)test::init<T>(1),q.pop());
    } 

    {
        HCE_INFO_FUNCTION_BODY(fname,"scope void mixed");
        auto lf = hce::scheduler::make();
        std::shared_ptr<hce::scheduler> sch = lf->get_scheduler();
        std::deque<hce::scope<>> scopes;

        std::function<void()> setup = [&]{
            scopes.push_back(hce::scope(sch->schedule(co_void())));
            scopes.push_back(hce::scope(
                sch->schedule(co_void()),
                sch->schedule(co_void())));
        };
        
        run_test(*sch, scopes, setup);
    } 

    {
        HCE_INFO_FUNCTION_BODY(fname,"scope add()ed void mixed");
        auto lf = hce::scheduler::make();
        std::shared_ptr<hce::scheduler> sch = lf->get_scheduler();
        std::deque<hce::scope<>> scopes;

        std::function<void()> setup = [&]{
            {
                hce::scope s;
                s.add(sch->schedule(co_void()));
                scopes.push_back(std::move(s));
            }

            {
                hce::scope s;
                s.add(sch->schedule(co_void()));
                s.add(sch->schedule(co_void()));
                scopes.push_back(std::move(s));
            }
        };
        
        run_test(*sch, scopes, setup);
    } 

    {
        HCE_INFO_FUNCTION_BODY(fname,"scope void mixed run successfully");
        test::queue<T> q;
        auto lf = hce::scheduler::make();
        std::shared_ptr<hce::scheduler> sch = lf->get_scheduler();
        std::deque<hce::scope<>> scopes;

        std::function<void()> setup = [&]{
            scopes.push_back(hce::scope(sch->schedule(co_push_T<T>(q,test::init<T>(3)))));
            auto awt2 = sch->schedule(co_push_T<T>(q,test::init<T>(2)));
            auto awt3 = sch->schedule(co_push_T<T>(q,test::init<T>(1)));
            scopes.push_back(hce::scope(
                std::move(awt2),
                std::move(awt3)));
        };

        run_test(*sch, scopes, setup);
        EXPECT_EQ(6,q.size());
        EXPECT_EQ((T)test::init<T>(3),q.pop());
        EXPECT_EQ((T)test::init<T>(2),q.pop());
        EXPECT_EQ((T)test::init<T>(1),q.pop());
        EXPECT_EQ((T)test::init<T>(3),q.pop());
        EXPECT_EQ((T)test::init<T>(2),q.pop());
        EXPECT_EQ((T)test::init<T>(1),q.pop());
    } 

    {
        HCE_INFO_FUNCTION_BODY(fname,"scope add()ed void mixed run successfully");
        test::queue<T> q;
        auto lf = hce::scheduler::make();
        std::shared_ptr<hce::scheduler> sch = lf->get_scheduler();
        std::deque<hce::scope<>> scopes;

        std::function<void()> setup = [&]{
            {
                hce::scope s;
                s.add(sch->schedule(co_push_T<T>(q,test::init<T>(3))));
                scopes.push_back(std::move(s));
            }

            {
                hce::scope s;
                s.add(sch->schedule(co_push_T<T>(q,test::init<T>(2))));
                s.add(sch->schedule(co_push_T<T>(q,test::init<T>(1))));
                scopes.push_back(std::move(s));
            }
        };

        run_test(*sch, scopes, setup);
        EXPECT_EQ(6,q.size());
        EXPECT_EQ((T)test::init<T>(3),q.pop());
        EXPECT_EQ((T)test::init<T>(2),q.pop());
        EXPECT_EQ((T)test::init<T>(1),q.pop());
        EXPECT_EQ((T)test::init<T>(3),q.pop());
        EXPECT_EQ((T)test::init<T>(2),q.pop());
        EXPECT_EQ((T)test::init<T>(1),q.pop());
    } 

    {
        HCE_INFO_FUNCTION_BODY(fname,"individually");
        auto lf = hce::scheduler::make();
        std::shared_ptr<hce::scheduler> sch = lf->get_scheduler();
        std::deque<hce::scope<>> scopes;

        std::function<void()> setup = [&]{
            scopes.push_back(hce::scope(sch->schedule(co_return_T<T>(test::init<T>(3)))));
            scopes.push_back(hce::scope(sch->schedule(co_return_T<T>(test::init<T>(2)))));
            scopes.push_back(hce::scope(sch->schedule(co_return_T<T>(test::init<T>(1)))));
        };

        run_test(*sch, scopes, setup);
    } 

    {
        HCE_INFO_FUNCTION_BODY(fname,"add()ed individually");
        auto lf = hce::scheduler::make();
        std::shared_ptr<hce::scheduler> sch = lf->get_scheduler();
        std::deque<hce::scope<>> scopes;

        std::function<void()> setup = [&]{
            {
                hce::scope s;
                s.add(sch->schedule(co_return_T<T>(test::init<T>(3))));
                scopes.push_back(std::move(s));
            }

            {
                hce::scope s;
                s.add(sch->schedule(co_return_T<T>(test::init<T>(2))));
                scopes.push_back(std::move(s));
            }

            {
                hce::scope s;
                s.add(sch->schedule(co_return_T<T>(test::init<T>(1))));
                scopes.push_back(std::move(s));
            }
        };

        run_test(*sch, scopes, setup);
    } 

    {
        HCE_INFO_FUNCTION_BODY(fname,"individually run successfully");
        test::queue<T> q;
        auto lf = hce::scheduler::make();
        std::shared_ptr<hce::scheduler> sch = lf->get_scheduler();
        std::deque<hce::scope<>> scopes;

        std::function<void()> setup = [&]{
            scopes.push_back(hce::scope(sch->schedule(co_push_T_return_T<T>(q,test::init<T>(3)))));
            scopes.push_back(hce::scope(sch->schedule(co_push_T_return_T<T>(q,test::init<T>(2)))));
            scopes.push_back(hce::scope(sch->schedule(co_push_T_return_T<T>(q,test::init<T>(1)))));
        };

        run_test(*sch, scopes, setup);
        EXPECT_EQ(6,q.size());
        EXPECT_EQ((T)test::init<T>(3),q.pop());
        EXPECT_EQ((T)test::init<T>(2),q.pop());
        EXPECT_EQ((T)test::init<T>(1),q.pop());
        EXPECT_EQ((T)test::init<T>(3),q.pop());
        EXPECT_EQ((T)test::init<T>(2),q.pop());
        EXPECT_EQ((T)test::init<T>(1),q.pop());
    } 

    {
        HCE_INFO_FUNCTION_BODY(fname,"add()ed individually run successfully");
        test::queue<T> q;
        auto lf = hce::scheduler::make();
        std::shared_ptr<hce::scheduler> sch = lf->get_scheduler();
        std::deque<hce::scope<>> scopes;

        std::function<void()> setup = [&]{
            {
                hce::scope s;
                s.add(sch->schedule(co_push_T_return_T<T>(q,test::init<T>(3))));
                scopes.push_back(std::move(s));
            }

            {
                hce::scope s;
                s.add(sch->schedule(co_push_T_return_T<T>(q,test::init<T>(2))));
                scopes.push_back(std::move(s));
            }

            {
                hce::scope s;
                s.add(sch->schedule(co_push_T_return_T<T>(q,test::init<T>(1))));
                scopes.push_back(std::move(s));
            }
        };

        run_test(*sch, scopes, setup);
        EXPECT_EQ(6,q.size());
        EXPECT_EQ((T)test::init<T>(3),q.pop());
        EXPECT_EQ((T)test::init<T>(2),q.pop());
        EXPECT_EQ((T)test::init<T>(1),q.pop());
        EXPECT_EQ((T)test::init<T>(3),q.pop());
        EXPECT_EQ((T)test::init<T>(2),q.pop());
        EXPECT_EQ((T)test::init<T>(1),q.pop());
    } 

    {
        HCE_INFO_FUNCTION_BODY(fname,"group");
        auto lf = hce::scheduler::make();
        std::shared_ptr<hce::scheduler> sch = lf->get_scheduler();
        std::deque<hce::scope<>> scopes;

        std::function<void()> setup = [&]{
            auto awt1 = sch->schedule(co_return_T<T>(test::init<T>(3)));
            auto awt2 = sch->schedule(co_return_T<T>(test::init<T>(2)));
            auto awt3 = sch->schedule(co_return_T<T>(test::init<T>(1)));
            scopes.push_back(hce::scope(
                std::move(awt1),
                std::move(awt2),
                std::move(awt3)));
        };

        run_test(*sch, scopes, setup);
    } 

    {
        HCE_INFO_FUNCTION_BODY(fname,"add()ed group");
        auto lf = hce::scheduler::make();
        std::shared_ptr<hce::scheduler> sch = lf->get_scheduler();
        std::deque<hce::scope<>> scopes;

        std::function<void()> setup = [&]{
            hce::scope s;
            auto awt1 = sch->schedule(co_return_T<T>(test::init<T>(3)));
            auto awt2 = sch->schedule(co_return_T<T>(test::init<T>(2)));
            auto awt3 = sch->schedule(co_return_T<T>(test::init<T>(1)));
            s.add(std::move(awt1), std::move(awt2), std::move(awt3));
            scopes.push_back(std::move(s));
        };

        run_test(*sch, scopes, setup);
    } 

    {
        HCE_INFO_FUNCTION_BODY(fname,"group run successfully");
        test::queue<T> q;
        auto lf = hce::scheduler::make();
        std::shared_ptr<hce::scheduler> sch = lf->get_scheduler();
        std::deque<hce::scope<>> scopes;

        std::function<void()> setup = [&]{
            auto awt1 = sch->schedule(co_push_T_return_T<T>(q,test::init<T>(3)));
            auto awt2 = sch->schedule(co_push_T_return_T<T>(q,test::init<T>(2)));
            auto awt3 = sch->schedule(co_push_T_return_T<T>(q,test::init<T>(1)));
            scopes.push_back(hce::scope(
                std::move(awt1),
                std::move(awt2),
                std::move(awt3)));
        };

        run_test(*sch, scopes, setup);
        EXPECT_EQ(6,q.size());
        EXPECT_EQ((T)test::init<T>(3),q.pop());
        EXPECT_EQ((T)test::init<T>(2),q.pop());
        EXPECT_EQ((T)test::init<T>(1),q.pop());
        EXPECT_EQ((T)test::init<T>(3),q.pop());
        EXPECT_EQ((T)test::init<T>(2),q.pop());
        EXPECT_EQ((T)test::init<T>(1),q.pop());
    } 

    {
        HCE_INFO_FUNCTION_BODY(fname,"add()ed group run successfully");
        test::queue<T> q;
        auto lf = hce::scheduler::make();
        std::shared_ptr<hce::scheduler> sch = lf->get_scheduler();
        std::deque<hce::scope<>> scopes;

        std::function<void()> setup = [&]{
            hce::scope s;
            auto awt1 = sch->schedule(co_push_T_return_T<T>(q,test::init<T>(3)));
            auto awt2 = sch->schedule(co_push_T_return_T<T>(q,test::init<T>(2)));
            auto awt3 = sch->schedule(co_push_T_return_T<T>(q,test::init<T>(1)));
            s.add(std::move(awt1), std::move(awt2), std::move(awt3));
            scopes.push_back(std::move(s));
        };

        run_test(*sch, scopes, setup);
        EXPECT_EQ(6,q.size());
        EXPECT_EQ((T)test::init<T>(3),q.pop());
        EXPECT_EQ((T)test::init<T>(2),q.pop());
        EXPECT_EQ((T)test::init<T>(1),q.pop());
        EXPECT_EQ((T)test::init<T>(3),q.pop());
        EXPECT_EQ((T)test::init<T>(2),q.pop());
        EXPECT_EQ((T)test::init<T>(1),q.pop());
    } 

    {
        HCE_INFO_FUNCTION_BODY(fname,"mixed");
        auto lf = hce::scheduler::make();
        std::shared_ptr<hce::scheduler> sch = lf->get_scheduler();
        std::deque<hce::scope<>> scopes;

        std::function<void()> setup = [&]{
            scopes.push_back(hce::scope(sch->schedule(co_return_T<T>(test::init<T>(3)))));
            auto awt2 = sch->schedule(co_return_T<T>(test::init<T>(2)));
            auto awt3 = sch->schedule(co_return_T<T>(test::init<T>(1)));
            scopes.push_back(hce::scope(std::move(awt2), std::move(awt3)));
        };
        
        run_test(*sch, scopes, setup);
    } 

    {
        HCE_INFO_FUNCTION_BODY(fname,"add()ed mixed");
        auto lf = hce::scheduler::make();
        std::shared_ptr<hce::scheduler> sch = lf->get_scheduler();
        std::deque<hce::scope<>> scopes;

        std::function<void()> setup = [&]{
            {
                hce::scope s;
                s.add(sch->schedule(co_return_T<T>(test::init<T>(3))));
                scopes.push_back(std::move(s));
            }

            {
                hce::scope s;
                auto awt2 = sch->schedule(co_return_T<T>(test::init<T>(2)));
                auto awt3 = sch->schedule(co_return_T<T>(test::init<T>(1)));
                s.add(std::move(awt2), std::move(awt3));
                scopes.push_back(std::move(s));
            }
        };
        
        run_test(*sch, scopes, setup);
    } 

    {
        HCE_INFO_FUNCTION_BODY(fname,"mixed run successfully");
        test::queue<T> q;
        auto lf = hce::scheduler::make();
        std::shared_ptr<hce::scheduler> sch = lf->get_scheduler();
        std::deque<hce::scope<>> scopes;

        std::function<void()> setup = [&]{
            scopes.push_back(hce::scope(sch->schedule(co_push_T_return_T<T>(q,test::init<T>(3)))));
            auto awt2 = sch->schedule(co_push_T_return_T<T>(q,test::init<T>(2)));
            auto awt3 = sch->schedule(co_push_T_return_T<T>(q,test::init<T>(1)));
            scopes.push_back(hce::scope(std::move(awt2), std::move(awt3)));
        };
        
        run_test(*sch, scopes, setup);
        EXPECT_EQ(6,q.size());
        EXPECT_EQ((T)test::init<T>(3),q.pop());
        EXPECT_EQ((T)test::init<T>(2),q.pop());
        EXPECT_EQ((T)test::init<T>(1),q.pop());
        EXPECT_EQ((T)test::init<T>(3),q.pop());
        EXPECT_EQ((T)test::init<T>(2),q.pop());
        EXPECT_EQ((T)test::init<T>(1),q.pop());
    } 

    {
        HCE_INFO_FUNCTION_BODY(fname,"add()ed mixed run successfully");
        test::queue<T> q;
        auto lf = hce::scheduler::make();
        std::shared_ptr<hce::scheduler> sch = lf->get_scheduler();
        std::deque<hce::scope<>> scopes;

        std::function<void()> setup = [&]{
            {
                hce::scope s;
                s.add(sch->schedule(co_push_T_return_T<T>(q,test::init<T>(3))));
                scopes.push_back(std::move(s));
            }

            {
                hce::scope s;
                auto awt2 = sch->schedule(co_push_T_return_T<T>(q,test::init<T>(2)));
                auto awt3 = sch->schedule(co_push_T_return_T<T>(q,test::init<T>(1)));
                s.add(std::move(awt2), std::move(awt3));
                scopes.push_back(std::move(s));
            }
        };
        
        run_test(*sch, scopes, setup);
        EXPECT_EQ(6,q.size());
        EXPECT_EQ((T)test::init<T>(3),q.pop());
        EXPECT_EQ((T)test::init<T>(2),q.pop());
        EXPECT_EQ((T)test::init<T>(1),q.pop());
        EXPECT_EQ((T)test::init<T>(3),q.pop());
        EXPECT_EQ((T)test::init<T>(2),q.pop());
        EXPECT_EQ((T)test::init<T>(1),q.pop());
    } 

    {
        HCE_INFO_FUNCTION_BODY(fname,"large add()");
        test::queue<T> q;
        auto lf = hce::scheduler::make();
        std::shared_ptr<hce::scheduler> sch = lf->get_scheduler();
        std::deque<hce::scope<>> scopes;

        std::function<void()> setup = [&]{
            hce::scope s;

            for(size_t i=0; i<1000; ++i) {
                s.add(sch->schedule(co_push_T_return_T<T>(q,test::init<T>(i))));
            }

            scopes.push_back(std::move(s));
        };
        
        run_test(*sch, scopes, setup);
        EXPECT_EQ(2000,q.size());

        for(size_t i=0; i<1000; ++i) {
            EXPECT_EQ((T)test::init<T>(i),q.pop());
        }

        for(size_t i=0; i<1000; ++i) {
            EXPECT_EQ((T)test::init<T>(i),q.pop());
        }
    } 

    return success_count;
}

}
}

TEST(scope, scope) {
    const size_t expected = 25;
    EXPECT_EQ(expected, test::scope::scope_T<int>());
    EXPECT_EQ(expected, test::scope::scope_T<unsigned int>());
    EXPECT_EQ(expected, test::scope::scope_T<size_t>());
    EXPECT_EQ(expected, test::scope::scope_T<float>());
    EXPECT_EQ(expected, test::scope::scope_T<double>());
    EXPECT_EQ(expected, test::scope::scope_T<char>());
    EXPECT_EQ(expected, test::scope::scope_T<void*>());
    EXPECT_EQ(expected, test::scope::scope_T<std::string>());
    EXPECT_EQ(expected, test::scope::scope_T<test::CustomObject>());
}
