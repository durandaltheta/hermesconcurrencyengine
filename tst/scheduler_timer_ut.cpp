//SPDX-License-Identifier: Apache-2.0
//Author: Blayne Dennis 
#include <deque>
#include <string>
#include <thread>
#include <chrono>
#include <vector>
#include <sstream>

#include "loguru.hpp"
#include "atomic.hpp"
#include "scheduler.hpp"

#include <gtest/gtest.h> 
#include "test_helpers.hpp"

namespace test {
namespace scheduler {

hce::chrono::duration absolute_difference(
    const hce::chrono::duration& d0, 
    const hce::chrono::duration& d1) {
    return d0 > d1 ? (d0 - d1) : (d1 - d0);
}

template <typename... As>
hce::co<bool> co_start(
        test::queue<hce::sid>& q,
        As&&... as) {
    hce::sid i;
    auto awt = hce::scheduler::local().start(i,as...);
    q.push(i);
    co_return co_await std::move(awt);
}

template <typename... As>
size_t start_As(As&&... as) {
    HCE_INFO_LOG(
            "start_As:milli timeout:%zu",
            hce::chrono::duration(as...).
                to_count<hce::chrono::milliseconds>());
    const size_t upper_bound_overslept_milli_ticks = 50;
    size_t success_count = 0;

    hce::chrono::duration d(as...);
    std::string s = d;

    auto lf = hce::scheduler::make();
    std::shared_ptr<hce::scheduler> sch = lf->scheduler();

    // thread timer timeout
    {
        auto now = hce::chrono::now();
        auto requested_sleep_ticks = 
                hce::chrono::duration(as...).
                    to_count<hce::chrono::milliseconds>();
        hce::chrono::time_point target_timeout(hce::chrono::duration(as...) + now);
        hce::sid i;
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


    // global timer timeout
    {
        auto now = hce::chrono::now();
        auto requested_sleep_ticks = 
                hce::chrono::duration(as...).
                    to_count<hce::chrono::milliseconds>();
        hce::chrono::time_point target_timeout(hce::chrono::duration(as...) + now);
        hce::sid i;
        EXPECT_TRUE((bool)hce::scheduler::global().start(i, as...));

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
        hce::sid i;
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

    // global thread sleep through timer timeout
    {
        auto now = hce::chrono::now();
        auto requested_sleep_ticks = 
                hce::chrono::duration(as...).
                    to_count<hce::chrono::milliseconds>();
        hce::chrono::time_point target_timeout(hce::chrono::duration(as...) + now);
        hce::sid i;
        auto awt = hce::scheduler::global().start(i, as...);

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

    // stacked thread timeouts 
    {
        const size_t max_timer_offset = 50;
        auto now = hce::chrono::now();
        auto requested_sleep_ticks = 
                hce::chrono::duration(as...).
                    to_count<hce::chrono::milliseconds>();
        hce::chrono::time_point target_timeout(hce::chrono::duration(as...) + hce::chrono::milliseconds(max_timer_offset) + now);
        std::deque<hce::awt<bool>> started_q;

        for(size_t c=max_timer_offset; c>0; --c) {
            hce::sid i;
            started_q.push_back(sch->start(i, hce::chrono::duration(as...) + hce::chrono::milliseconds(c)));
        }

        for(size_t c=max_timer_offset; c>0; --c) {
            EXPECT_TRUE((bool)std::move(started_q.front()));
            started_q.pop_front();
        }

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
        test::queue<hce::sid> q;
        auto now = hce::chrono::now();
        auto requested_sleep_ticks = 
                hce::chrono::duration(as...).
                    to_count<hce::chrono::milliseconds>();
        hce::chrono::time_point target_timeout(hce::chrono::duration(as...) + now);
        
        EXPECT_TRUE((bool)sch->schedule(co_start(q, as...)));

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
        test::queue<hce::sid> q;
        auto now = hce::chrono::now();
        auto requested_sleep_ticks = 
                hce::chrono::duration(as...).
                    to_count<hce::chrono::milliseconds>();
        hce::chrono::time_point target_timeout(hce::chrono::duration(as...) + now);
        EXPECT_TRUE((bool)hce::schedule(co_start(q, as...)));

        auto done = hce::chrono::now();
        auto slept_ticks = absolute_difference(done,now).to_count<hce::chrono::milliseconds>();
        auto overslept_ticks = absolute_difference(target_timeout, done).to_count<hce::chrono::milliseconds>();

        // ensure we slept at least the correct amount of time
        EXPECT_GE(slept_ticks, requested_sleep_ticks); 

        // ensure we didn't sleep past the upper bound
        EXPECT_LT(overslept_ticks, upper_bound_overslept_milli_ticks);

        ++success_count;
    }

    // stacked coroutine timeouts 
    {
        test::queue<hce::sid> q;
        const size_t max_timer_offset = 50;
        auto now = hce::chrono::now();
        auto requested_sleep_ticks = 
                hce::chrono::duration(as...).
                    to_count<hce::chrono::milliseconds>();
        hce::chrono::time_point target_timeout(hce::chrono::duration(as...) + hce::chrono::milliseconds(max_timer_offset) + now);
        std::deque<hce::awt<bool>> started_q;

        for(size_t c=max_timer_offset; c>0; --c) {
            started_q.push_back(sch->schedule(co_start(q, hce::chrono::duration(as...) + hce::chrono::milliseconds(c))));
        }

        for(size_t c=max_timer_offset; c>0; --c) {
            EXPECT_TRUE((bool)std::move(started_q.front()));
            started_q.pop_front();
        }

        auto done = hce::chrono::now();
        auto slept_ticks = absolute_difference(done,now).to_count<hce::chrono::milliseconds>();
        auto overslept_ticks = absolute_difference(target_timeout, done).to_count<hce::chrono::milliseconds>();

        // ensure we slept at least the correct amount of time
        EXPECT_GE(slept_ticks, requested_sleep_ticks); 

        // ensure we didn't sleep past the upper bound
        EXPECT_LT(overslept_ticks, upper_bound_overslept_milli_ticks);

        ++success_count;
    }

    return success_count; 
}

}
}

TEST(scheduler, start) {
    const size_t expected_successes = 8;
    EXPECT_EQ(expected_successes,test::scheduler::start_As(hce::chrono::milliseconds(50)));
    EXPECT_EQ(expected_successes,test::scheduler::start_As(hce::chrono::microseconds(50000)));
    EXPECT_EQ(expected_successes,test::scheduler::start_As(hce::chrono::nanoseconds(50000000)));
    EXPECT_EQ(expected_successes,test::scheduler::start_As(hce::chrono::duration(hce::chrono::milliseconds(50))));
    EXPECT_EQ(expected_successes,test::scheduler::start_As(hce::chrono::duration(hce::chrono::microseconds(50000))));
    EXPECT_EQ(expected_successes,test::scheduler::start_As(hce::chrono::duration(hce::chrono::nanoseconds(50000000))));
    EXPECT_EQ(expected_successes,test::scheduler::start_As(hce::chrono::time_point(hce::chrono::duration(hce::chrono::milliseconds(50)))));
    EXPECT_EQ(expected_successes,test::scheduler::start_As(hce::chrono::time_point(hce::chrono::duration(hce::chrono::microseconds(50000)))));
    EXPECT_EQ(expected_successes,test::scheduler::start_As(hce::chrono::time_point(hce::chrono::duration(hce::chrono::nanoseconds(50000000)))));
}

namespace test {
namespace scheduler {

template <typename... As>
hce::co<bool> co_sleep(
        test::queue<int>& q,
        As&&... as) {
    auto awt = hce::scheduler::local().sleep(as...);
    co_return co_await std::move(awt);
}

template <typename... As>
size_t sleep_As(As&&... as) {
    const size_t upper_bound_overslept_milli_ticks = 50;
    size_t success_count = 0;

    auto lf = hce::scheduler::make();
    std::shared_ptr<hce::scheduler> sch = lf->scheduler();

    // thread timer timeout
    {
        auto now = hce::chrono::now();
        auto requested_sleep_ticks = 
                hce::chrono::duration(as...).
                    to_count<hce::chrono::milliseconds>();
        hce::chrono::time_point target_timeout(hce::chrono::duration(as...) + now);
        EXPECT_TRUE((bool)sch->sleep(as...));

        auto done = hce::chrono::now();
        auto slept_ticks = absolute_difference(done,now).to_count<hce::chrono::milliseconds>();
        auto overslept_ticks = absolute_difference(target_timeout, done).to_count<hce::chrono::milliseconds>();

        // ensure we slept at least the correct amount of time
        EXPECT_GE(slept_ticks, requested_sleep_ticks); 

        // ensure we didn't sleep past the upper bound
        EXPECT_LT(overslept_ticks, upper_bound_overslept_milli_ticks);

        ++success_count;
    }

    // global timer timeout
    {
        auto now = hce::chrono::now();
        auto requested_sleep_ticks = 
                hce::chrono::duration(as...).
                    to_count<hce::chrono::milliseconds>();
        hce::chrono::time_point target_timeout(hce::chrono::duration(as...) + now);
        EXPECT_TRUE((bool)hce::sleep(as...));

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
        auto awt = sch->sleep(as...);

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

    // thread sleep through timer timeout
    {
        auto now = hce::chrono::now();
        auto requested_sleep_ticks = 
                hce::chrono::duration(as...).
                    to_count<hce::chrono::milliseconds>();
        hce::chrono::time_point target_timeout(hce::chrono::duration(as...) + now);
        auto awt = hce::sleep(as...);

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

    // stacked thread timeouts 
    {
        const size_t max_timer_offset = 50;
        auto now = hce::chrono::now();
        auto requested_sleep_ticks = 
                hce::chrono::duration(as...).
                    to_count<hce::chrono::milliseconds>();
        hce::chrono::time_point target_timeout(hce::chrono::duration(as...) + hce::chrono::milliseconds(max_timer_offset) + now);
        std::deque<hce::awt<bool>> started_q;

        for(size_t c=max_timer_offset; c>0; --c) {
            started_q.push_back(sch->sleep(hce::chrono::duration(as...) + hce::chrono::milliseconds(c)));
        }

        for(size_t c=max_timer_offset; c>0; --c) {
            EXPECT_TRUE((bool)std::move(started_q.front()));
            started_q.pop_front();
        }

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
        test::queue<int> q;
        auto now = hce::chrono::now();
        auto requested_sleep_ticks = 
                hce::chrono::duration(as...).
                    to_count<hce::chrono::milliseconds>();
        hce::chrono::time_point target_timeout(hce::chrono::duration(as...) + now);
        EXPECT_TRUE((bool)sch->schedule(co_sleep(q, as...)));

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
        test::queue<int> q;
        auto now = hce::chrono::now();
        auto requested_sleep_ticks = 
                hce::chrono::duration(as...).
                    to_count<hce::chrono::milliseconds>();
        hce::chrono::time_point target_timeout(hce::chrono::duration(as...) + now);
        EXPECT_TRUE((bool)hce::schedule(co_sleep(q, as...)));

        auto done = hce::chrono::now();
        auto slept_ticks = absolute_difference(done,now).to_count<hce::chrono::milliseconds>();
        auto overslept_ticks = absolute_difference(target_timeout, done).to_count<hce::chrono::milliseconds>();

        // ensure we slept at least the correct amount of time
        EXPECT_GE(slept_ticks, requested_sleep_ticks); 

        // ensure we didn't sleep past the upper bound
        EXPECT_LT(overslept_ticks, upper_bound_overslept_milli_ticks);

        ++success_count;
    }

    // stacked coroutine timeouts 
    {
        test::queue<int> q;
        const size_t max_timer_offset = 50;
        auto now = hce::chrono::now();
        auto requested_sleep_ticks = 
                hce::chrono::duration(as...).
                    to_count<hce::chrono::milliseconds>();
        hce::chrono::time_point target_timeout(hce::chrono::duration(as...) + hce::chrono::milliseconds(max_timer_offset) + now);
        std::deque<hce::awt<bool>> started_q;

        for(size_t c=max_timer_offset; c>0; --c) {
            started_q.push_back(sch->schedule(co_sleep(q, hce::chrono::duration(as...) + hce::chrono::milliseconds(c))));
        }

        for(size_t c=max_timer_offset; c>0; --c) {
            EXPECT_TRUE((bool)std::move(started_q.front()));
            started_q.pop_front();
        }

        auto done = hce::chrono::now();
        auto slept_ticks = absolute_difference(done,now).to_count<hce::chrono::milliseconds>();
        auto overslept_ticks = absolute_difference(target_timeout, done).to_count<hce::chrono::milliseconds>();

        // ensure we slept at least the correct amount of time
        EXPECT_GE(slept_ticks, requested_sleep_ticks); 

        // ensure we didn't sleep past the upper bound
        EXPECT_LT(overslept_ticks, upper_bound_overslept_milli_ticks);

        ++success_count;
    }

    return success_count; 
}

}
}

TEST(scheduler, sleep) {
    const size_t expected_successes = 8;
    EXPECT_EQ(expected_successes,test::scheduler::sleep_As(hce::chrono::milliseconds(50)));
    EXPECT_EQ(expected_successes,test::scheduler::sleep_As(hce::chrono::microseconds(50000)));
    EXPECT_EQ(expected_successes,test::scheduler::sleep_As(hce::chrono::nanoseconds(50000000)));
    EXPECT_EQ(expected_successes,test::scheduler::sleep_As(hce::chrono::duration(hce::chrono::milliseconds(50))));
    EXPECT_EQ(expected_successes,test::scheduler::sleep_As(hce::chrono::duration(hce::chrono::microseconds(50000))));
    EXPECT_EQ(expected_successes,test::scheduler::sleep_As(hce::chrono::duration(hce::chrono::nanoseconds(50000000))));
    EXPECT_EQ(expected_successes,test::scheduler::sleep_As(hce::chrono::time_point(hce::chrono::duration(hce::chrono::milliseconds(50)))));
    EXPECT_EQ(expected_successes,test::scheduler::sleep_As(hce::chrono::time_point(hce::chrono::duration(hce::chrono::microseconds(50000)))));
    EXPECT_EQ(expected_successes,test::scheduler::sleep_As(hce::chrono::time_point(hce::chrono::duration(hce::chrono::nanoseconds(50000000)))));
}

namespace test {
namespace scheduler {

template <typename... As>
size_t cancel_As(As&&... as) {
    hce::stringstream ss;
    ss << "cancel_As:" 
       << hce::chrono::duration(as...).to_count<hce::chrono::milliseconds>();
    hce::string fname = ss.str();
    //HCE_INFO_LOG(
    HCE_WARNING_LOG("%s",fname.c_str());
    size_t success_count = 0;

    auto lf = hce::scheduler::make();
    std::shared_ptr<hce::scheduler> sch = lf->scheduler();

    // thread timer cancel
    {
        HCE_WARNING_FUNCTION_BODY(fname, "thread timer cancel");
        test::queue<hce::sid> q;

        std::thread sleeping_thd([&]{
            hce::sid i;
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

            // ensure we slept at least the correct amount of time
            EXPECT_LT(slept_ticks, requested_sleep_ticks); 
        });

        hce::sid i = q.pop();
        EXPECT_TRUE(sch->cancel(i));
        sleeping_thd.join();

        ++success_count;
    }

    // thread global timer cancel
    {
        HCE_WARNING_FUNCTION_BODY(fname, "thread global timer cancel");
        test::queue<hce::sid> q;

        std::thread sleeping_thd([&]{
            hce::sid i;
            auto now = hce::chrono::now();
            auto requested_sleep_ticks = 
                    hce::chrono::duration(as...).
                        to_count<hce::chrono::milliseconds>();
            hce::chrono::time_point target_timeout(hce::chrono::duration(as...) + now);
            auto awt = hce::scheduler::global().start(i, as...);
            q.push(i);
            EXPECT_FALSE((bool)std::move(awt));

            auto done = hce::chrono::now();
            auto slept_ticks = absolute_difference(done,now).to_count<hce::chrono::milliseconds>();

            // ensure we slept at least the correct amount of time
            EXPECT_LT(slept_ticks, requested_sleep_ticks); 
        });

        hce::sid i = q.pop();
        EXPECT_TRUE(hce::scheduler::global().cancel(i));
        sleeping_thd.join();

        ++success_count;
    }

    // coroutine timer cancel
    {
        HCE_WARNING_FUNCTION_BODY(fname, "coroutine timer cancel");
        test::queue<hce::sid> q;
        auto now = hce::chrono::now();
        auto requested_sleep_ticks = 
            hce::chrono::duration(as...).
                to_count<hce::chrono::milliseconds>();
        hce::chrono::time_point target_timeout(hce::chrono::duration(as...) + now);
        auto awt = sch->schedule(co_start(q, as...));
        hce::sid i = q.pop();
        sch->cancel(i);

        EXPECT_FALSE((bool)std::move(awt));

        auto done = hce::chrono::now();
        auto slept_ticks = absolute_difference(done,now).to_count<hce::chrono::milliseconds>();

        // ensure we slept at least the correct amount of time
        EXPECT_LT(slept_ticks, requested_sleep_ticks); 

        ++success_count;
    }

    // coroutine global timer cancel
    {
        HCE_WARNING_FUNCTION_BODY(fname, "coroutine global timer cancel");
        test::queue<hce::sid> q;
        auto now = hce::chrono::now();
        auto requested_sleep_ticks = 
            hce::chrono::duration(as...).
                to_count<hce::chrono::milliseconds>();
        hce::chrono::time_point target_timeout(hce::chrono::duration(as...) + now);
        auto awt = hce::schedule(co_start(q, as...));
        hce::sid i = q.pop();
        hce::scheduler::global().cancel(i);

        EXPECT_FALSE((bool)std::move(awt));

        auto done = hce::chrono::now();
        auto slept_ticks = absolute_difference(done,now).to_count<hce::chrono::milliseconds>();

        // ensure we slept at least the correct amount of time
        EXPECT_LT(slept_ticks, requested_sleep_ticks); 

        ++success_count;
    }

    return success_count; 
}

}
}

TEST(scheduler, cancel) {
    const size_t expected_successes = 4;
    EXPECT_EQ(expected_successes,test::scheduler::cancel_As(hce::chrono::milliseconds(50)));
    EXPECT_EQ(expected_successes,test::scheduler::cancel_As(hce::chrono::microseconds(50000)));
    EXPECT_EQ(expected_successes,test::scheduler::cancel_As(hce::chrono::nanoseconds(50000000)));
    EXPECT_EQ(expected_successes,test::scheduler::cancel_As(hce::chrono::duration(hce::chrono::milliseconds(50))));
    EXPECT_EQ(expected_successes,test::scheduler::cancel_As(hce::chrono::duration(hce::chrono::microseconds(50000))));
    EXPECT_EQ(expected_successes,test::scheduler::cancel_As(hce::chrono::duration(hce::chrono::nanoseconds(50000000))));
    EXPECT_EQ(expected_successes,test::scheduler::cancel_As(hce::chrono::time_point(hce::chrono::duration(hce::chrono::milliseconds(50)))));
    EXPECT_EQ(expected_successes,test::scheduler::cancel_As(hce::chrono::time_point(hce::chrono::duration(hce::chrono::microseconds(50000)))));
    EXPECT_EQ(expected_successes,test::scheduler::cancel_As(hce::chrono::time_point(hce::chrono::duration(hce::chrono::nanoseconds(50000000)))));
}
