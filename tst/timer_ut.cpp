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
#include "chrono.hpp"
#include "timer.hpp"

#include <gtest/gtest.h> 
#include "test_helpers.hpp"

namespace test {
namespace timer {
       
// the maximum millisecond count (effectively loop iterations) that can be 
// added to a timer duration when stacking timers
const size_t max_timer_offset = 50;

// times each test should iterate different timeouts
const unsigned int iterate_limit = 50;
const unsigned int medium_iterate_limit = 25;
const unsigned int long_iterate_limit = 15;

// 1/100th of a second
const unsigned int milli_one_hundreth_second(10); 

// 1/40th of a second
const unsigned int milli_one_fortieth_second(50); 

size_t sleep_total = 0;
size_t oversleep_total = 0;
size_t running_total = 0;
size_t missedrunning_total = 0;

const hce::chrono::duration upper_bound_overslept_milli_ticks(
    std::chrono::milliseconds(50));

hce::chrono::duration absolute_difference(
    const hce::chrono::time_point& d0, 
    const hce::chrono::time_point& d1) {
    return d0 > d1 ? (d0 - d1) : (d1 - d0);
}

void validate_test(std::optional<double> check_sleep, 
                   std::optional<double> check_running, 
                   std::optional<double> check_busywait) {
    if(check_sleep) {
        size_t sleep_success = sleep_total - oversleep_total;
        double sleep_success_percentage = (((double)sleep_success) / sleep_total) * 100;
        std::cout << "sleep_total: " << sleep_total << std::endl;
        std::cout << "oversleep_total: " << oversleep_total << std::endl;
        std::cout << "timer target window timeout success: " << sleep_success_percentage << "%" << std::endl;
        EXPECT_TRUE(sleep_total);
        EXPECT_GT(sleep_success_percentage, *check_sleep);
    }

    if(check_running) {
        size_t running_success = running_total - missedrunning_total;
        double running_success_percentage = (((double)running_success) / running_total) * 100;
        std::cout << "running_total: " << running_total << std::endl;
        std::cout << "missedrunning_total: " << missedrunning_total << std::endl;
        std::cout << "timer target window running check success: " << running_success_percentage << "%" <<std::endl;
        EXPECT_TRUE(running_total);
        EXPECT_GT(running_success_percentage, *check_running);
    }

    if(check_busywait) {
        hce::timer::service::ticks ticks = hce::timer::service::get().get_ticks();
        double busy_wait_rate = ((double)(ticks.busywait)/(ticks.runtime)) * 100;
        std::cout 
            << "timer service busy-wait microsecond threshold: " 
            << std::chrono::duration_cast<std::chrono::microseconds>(
                hce::config::timer::busy_wait_threshold()).count()
            << std::endl;
        std::cout << "timer service busy-wait runtime rate: " <<  busy_wait_rate << "%" << std::endl;
        EXPECT_LT(busy_wait_rate, *check_busywait);
    }
}

hce::co<bool> co_timer(
        test::queue<hce::sid>& q,
        const hce::chrono::duration dur) {
    hce::sid i;
    auto awt = hce::timer::start(i,dur);
    q.push(i);
    co_return co_await awt;
}

hce::co<bool> co_timer(
        test::queue<hce::sid>& q,
        const hce::chrono::time_point tp) {
    hce::sid i;
    auto awt = hce::timer::start(i,tp);
    q.push(i);
    co_return co_await awt;
}

template <typename... As>
size_t start(As&&... as) {
    const hce::chrono::duration dur(as...);

    HCE_HIGH_FUNCTION_ENTER(
            "start",
            sleep_total,
            oversleep_total,
            hce::chrono::to<std::chrono::nanoseconds>(dur).count());
    size_t success_count = 0;

    auto check_overslept = [&](hce::chrono::time_point target, hce::chrono::time_point done){
        hce::chrono::duration overslept_ticks = absolute_difference(done, target);

        if(upper_bound_overslept_milli_ticks < overslept_ticks) {
            HCE_INFO_FUNCTION_BODY(
                "test::timer::start","[OVERSLEPT] missed target milli:", 
                hce::chrono::to<std::chrono::milliseconds>(overslept_ticks).count(), 
                ", overslept upper bound milli:", 
                hce::chrono::to<std::chrono::milliseconds>(upper_bound_overslept_milli_ticks).count());
            ++oversleep_total;
        }
    };

    auto check_running = [&](hce::sid& s, bool expected) {
        ++running_total;

        if(expected != hce::timer::running(s)) {
            ++missedrunning_total;
        }
    };

    struct data {
        data(hce::awt<bool>&& a, const hce::sid& s) :
            awt(std::move(a)),
            sid(s)
        { }

        hce::awt<bool> awt;
        hce::sid sid;
    };

    {
        HCE_HIGH_FUNCTION_ENTER("start","thread timer duration");
        auto requested_sleep_ticks = 
            hce::chrono::to<std::chrono::nanoseconds>(dur).count();

        auto now = hce::chrono::now();
        hce::chrono::time_point target(dur + now);
        hce::sid i;
        auto awt = hce::timer::start(i, dur);
        check_running(i, true);
        EXPECT_TRUE((bool)awt);
        check_running(i, false);
        ++sleep_total;

        auto done = hce::chrono::now();
        auto slept_ticks = 
            hce::chrono::to<std::chrono::nanoseconds>(
                absolute_difference(done,now)).count();

        // ensure we slept at least the correct amount of time
        EXPECT_GE(slept_ticks, requested_sleep_ticks); 

        // ensure we didn't sleep past the upper bound
        check_overslept(target, done);

        ++success_count;
    }

    {
        HCE_HIGH_FUNCTION_ENTER("start","thread timer time_point");
        auto requested_sleep_ticks = 
            hce::chrono::to<std::chrono::nanoseconds>(dur).count();

        auto now = hce::chrono::now();
        hce::chrono::time_point target(dur + now);
        hce::sid i;
        auto awt = hce::timer::start(i, target);
        check_running(i, true);
        EXPECT_TRUE((bool)awt);
        check_running(i, false);
        ++sleep_total;

        auto done = hce::chrono::now();
        auto slept_ticks = 
            hce::chrono::to<std::chrono::nanoseconds>(
                absolute_difference(done,now)).count();

        // ensure we slept at least the correct amount of time
        EXPECT_GE(slept_ticks, requested_sleep_ticks); 

        // ensure we didn't sleep past the upper bound
        check_overslept(target, done);

        ++success_count;
    }

    {
        HCE_HIGH_FUNCTION_ENTER("start","thread sleep through timer duration");
        auto now = hce::chrono::now();
        auto requested_sleep_ticks = 
            hce::chrono::to<std::chrono::nanoseconds>(dur).count();

        hce::chrono::time_point target(dur + now);
        hce::sid i;
        auto awt = hce::timer::start(i, dur);
        check_running(i, true);
        ++sleep_total;

        // ensure we sleep for the entire normal 
        std::this_thread::sleep_for(dur);

        // the awaitable should return immediately
        EXPECT_TRUE((bool)awt);
        check_running(i, false);

        auto done = hce::chrono::now();
        auto slept_ticks = 
            hce::chrono::to<std::chrono::nanoseconds>(
                absolute_difference(done,now)).count();

        // ensure we slept at least the correct amount of time
        EXPECT_GE(slept_ticks, requested_sleep_ticks); 

        // ensure we didn't sleep past the upper bound
        check_overslept(target, done);

        ++success_count;
    }

    {
        HCE_HIGH_FUNCTION_ENTER("start","thread sleep through timer duration");
        auto now = hce::chrono::now();
        auto requested_sleep_ticks = 
            hce::chrono::to<std::chrono::nanoseconds>(dur).count();

        hce::chrono::time_point target(dur + now);
        hce::sid i;
        auto awt = hce::timer::start(i, target);
        check_running(i, true);
        ++sleep_total;

        // ensure we sleep for the entire normal 
        std::this_thread::sleep_for(dur);

        // the awaitable should return immediately
        EXPECT_TRUE((bool)awt);
        check_running(i, false);

        auto done = hce::chrono::now();
        auto slept_ticks = 
            hce::chrono::to<std::chrono::nanoseconds>(
                absolute_difference(done,now)).count();

        // ensure we slept at least the correct amount of time
        EXPECT_GE(slept_ticks, requested_sleep_ticks); 

        // ensure we didn't sleep past the upper bound
        check_overslept(target, done);

        ++success_count;
    }

    {
        HCE_HIGH_FUNCTION_ENTER("start","stacked thread duration");
        auto now = hce::chrono::now();
        auto requested_sleep_ticks = 
            hce::chrono::to<std::chrono::nanoseconds>(dur).count();
        hce::chrono::time_point target(dur + std::chrono::milliseconds(max_timer_offset) + now);

        std::deque<data> started_q;

        for(size_t c=max_timer_offset; c>0; --c) {
            hce::sid i;
            started_q.push_back(
                data(hce::timer::start(
                         i, 
                         dur + std::chrono::milliseconds(c)),
                     i));
            check_running(i, true);
        }

        // count all timers as 1 sleep because we only check overslept once
        ++sleep_total;

        for(size_t c=max_timer_offset; c>0; --c) {
            auto data = std::move(started_q.front());
            started_q.pop_front();
            EXPECT_TRUE((bool)(data.awt));
            check_running(data.sid, false);
        }

        auto done = hce::chrono::now();
        auto slept_ticks = 
            hce::chrono::to<std::chrono::nanoseconds>(
                absolute_difference(done,now)).count();

        // ensure we slept at least the correct amount of time
        EXPECT_GE(slept_ticks, requested_sleep_ticks); 

        // ensure we didn't sleep past the upper bound
        check_overslept(target, done);

        ++success_count;
    }

    {
        HCE_HIGH_FUNCTION_ENTER("start","stacked thread time_point");
        auto now = hce::chrono::now();
        auto requested_sleep_ticks = 
            hce::chrono::to<std::chrono::nanoseconds>(dur).count();
        hce::chrono::time_point target(dur + std::chrono::milliseconds(max_timer_offset) + now);

        std::deque<data> started_q;

        for(size_t c=max_timer_offset; c>0; --c) {
            hce::sid i;
            started_q.push_back(
                data(hce::timer::start(
                         i, 
                         now + dur + std::chrono::milliseconds(c)),
                     i));
            check_running(i, true);
        }

        // count all timers as 1 sleep because we only check overslept once
        ++sleep_total;

        for(size_t c=max_timer_offset; c>0; --c) {
            auto data = std::move(started_q.front());
            started_q.pop_front();
            EXPECT_TRUE((bool)(data.awt));
            check_running(data.sid, false);
        }

        auto done = hce::chrono::now();
        auto slept_ticks = 
            hce::chrono::to<std::chrono::nanoseconds>(
                absolute_difference(done,now)).count();

        // ensure we slept at least the correct amount of time
        EXPECT_GE(slept_ticks, requested_sleep_ticks); 

        // ensure we didn't sleep past the upper bound
        check_overslept(target, done);

        ++success_count;
    }

    {
        HCE_HIGH_FUNCTION_ENTER("start","coroutine timer duration");
        test::queue<hce::sid> q;
        auto now = hce::chrono::now();
        auto requested_sleep_ticks = 
            hce::chrono::to<std::chrono::nanoseconds>(dur).count();
        hce::chrono::time_point target(dur + now);
        auto awt = hce::schedule(co_timer(q, dur));
        hce::sid i = q.pop();
        EXPECT_TRUE((bool)awt);
        check_running(i, false);
        ++sleep_total;

        auto done = hce::chrono::now();
        auto slept_ticks = 
            hce::chrono::to<std::chrono::nanoseconds>(
                absolute_difference(done,now)).count();

        // ensure we slept at least the correct amount of time
        EXPECT_GE(slept_ticks, requested_sleep_ticks); 

        // ensure we didn't sleep past the upper bound
        check_overslept(target, done);

        ++success_count;
    }

    {
        HCE_HIGH_FUNCTION_ENTER("start","coroutine timer time_point");
        test::queue<hce::sid> q;
        auto now = hce::chrono::now();
        auto requested_sleep_ticks = 
            hce::chrono::to<std::chrono::nanoseconds>(dur).count();
        hce::chrono::time_point target(dur + now);
        auto awt = hce::schedule(co_timer(q, target));
        hce::sid i = q.pop();
        EXPECT_TRUE((bool)awt);
        check_running(i, false);
        ++sleep_total;

        auto done = hce::chrono::now();
        auto slept_ticks = 
            hce::chrono::to<std::chrono::nanoseconds>(
                absolute_difference(done,now)).count();

        // ensure we slept at least the correct amount of time
        EXPECT_GE(slept_ticks, requested_sleep_ticks); 

        // ensure we didn't sleep past the upper bound
        check_overslept(target, done);

        ++success_count;
    }

    {
        HCE_HIGH_FUNCTION_ENTER("start","stacked coroutine duration");
        test::queue<hce::sid> q;
        auto now = hce::chrono::now();
        auto requested_sleep_ticks = 
            hce::chrono::to<std::chrono::nanoseconds>(dur).count();

        hce::chrono::time_point target(dur + std::chrono::milliseconds(max_timer_offset) + now);
        std::deque<hce::awt<bool>> started_q;

        for(size_t c=max_timer_offset; c>0; --c) {
            std::chrono::milliseconds milli_offset(c);
            hce::chrono::duration timeout = dur + milli_offset;
            started_q.push_back(hce::schedule(co_timer(q, timeout)));
        }

        // count all timers as 1 sleep because we only check overslept once
        ++sleep_total;

        for(size_t c=max_timer_offset; c>0; --c) {
            hce::sid i = q.pop();
            auto awt = std::move(started_q.front());
            started_q.pop_front();
            EXPECT_TRUE((bool)(i));
            EXPECT_TRUE((bool)(awt));
            check_running(i, false);
        }

        auto done = hce::chrono::now();
        auto slept_ticks = 
            hce::chrono::to<std::chrono::nanoseconds>(
                absolute_difference(done,now)).count();

        // ensure we slept at least the correct amount of time
        EXPECT_GE(slept_ticks, requested_sleep_ticks); 

        // ensure we didn't sleep past the upper bound
        check_overslept(target, done);

        ++success_count;
    }

    {
        HCE_HIGH_FUNCTION_ENTER("start","stacked coroutine time_point");
        test::queue<hce::sid> q;
        auto now = hce::chrono::now();
        auto requested_sleep_ticks = 
            hce::chrono::to<std::chrono::nanoseconds>(dur).count();

        hce::chrono::time_point target(dur + std::chrono::milliseconds(max_timer_offset) + now);
        std::deque<hce::awt<bool>> started_q;

        for(size_t c=max_timer_offset; c>0; --c) {
            std::chrono::milliseconds milli_offset(c);
            hce::chrono::duration timeout = dur + milli_offset;
            started_q.push_back(hce::schedule(co_timer(q, now + timeout)));
        }

        // count all timers as 1 sleep because we only check overslept once
        ++sleep_total;

        for(size_t c=max_timer_offset; c>0; --c) {
            hce::sid i = q.pop();
            auto awt = std::move(started_q.front());
            started_q.pop_front();
            EXPECT_TRUE((bool)(i));
            EXPECT_TRUE((bool)(awt));
            check_running(i, false);
        }

        auto done = hce::chrono::now();
        auto slept_ticks = 
            hce::chrono::to<std::chrono::nanoseconds>(
                absolute_difference(done,now)).count();

        // ensure we slept at least the correct amount of time
        EXPECT_GE(slept_ticks, requested_sleep_ticks); 

        // ensure we didn't sleep past the upper bound
        check_overslept(target, done);

        ++success_count;
    }

    HCE_HIGH_FUNCTION_ENTER("start","done");
    return success_count; 
}

}
}

class timer : public ::testing::Test {
protected:
    // This function is called before each test.
    void SetUp() override {
        test::timer::sleep_total = 0;
        test::timer::oversleep_total = 0;
        test::timer::running_total = 0;
        test::timer::missedrunning_total = 0;

        // reset ticks for calculation
        hce::timer::service::get().reset_ticks();
    }

    // This function is called after each test.
    void TearDown() override {
        // Cleanup code here
    }
};

TEST_F(timer, start_short) {
    size_t expected_successes = 10;

    // Iterate through a range of small millisecond timeouts to get a really 
    // solid set of results for calculating a success rate for short sleeps. 
    //
    // Intentionally start with an index of 0 to get immediate timeouts.
    for(unsigned int i=0; i<test::timer::iterate_limit; ++i) {
        HCE_INFO_LOG("TEST_F(timer,start_short) milli:%u",i);
        // test that we can wait on timers using a variety of hce::chrono::duration variants 
        EXPECT_EQ(expected_successes,test::timer::start(std::chrono::milliseconds(i)));
        EXPECT_EQ(expected_successes,test::timer::start(std::chrono::microseconds(1000*i)));
        EXPECT_EQ(expected_successes,test::timer::start(std::chrono::nanoseconds(1000000*i)));
    }

    test::timer::validate_test({95.0},{98.0},{25.0});
}

TEST_F(timer, start_medium) {
    size_t expected_successes = 10;
   
    // iterate through timeouts that are signficant millisecond count, to 
    // ensure we test medium sleeps which shouldn't busy-wait as much
    for(unsigned int i=1; i<test::timer::medium_iterate_limit; ++i) {
        const unsigned int milli_dur = i * test::timer::milli_one_hundreth_second;
        HCE_INFO_LOG("TEST_F(timer,start_medium) milli:%u",milli_dur);
        EXPECT_EQ(expected_successes,test::timer::start(std::chrono::milliseconds(milli_dur)));
    }

    test::timer::validate_test({90.0},{98.0},{5.0});
}

TEST_F(timer, start_long) {
    size_t expected_successes = 10;
   
    // iterate through timeouts that are signficant portions of a second, to 
    // ensure we test longer sleeps which shouldn't busy-wait hardly ever
    for(unsigned int i=1; i<test::timer::long_iterate_limit; ++i) {
        const unsigned int milli_dur = i * test::timer::milli_one_fortieth_second;
        HCE_INFO_LOG("TEST_F(timer,start_long) milli:%u",milli_dur);
        EXPECT_EQ(expected_successes,test::timer::start(std::chrono::milliseconds(milli_dur)));
    }

    test::timer::validate_test({90.0},{98.0},{1.0});
}

namespace test {
namespace timer {

hce::co<void> co_sleep(const hce::chrono::duration dur) {
    co_await hce::sleep(dur);
    co_return;
}

hce::co<void> co_sleep(const hce::chrono::time_point tp) {
    co_await hce::sleep(tp);
    co_return;
}

template <typename... As>
size_t sleep(As&&... as) {
    const hce::chrono::duration dur(as...);

    HCE_HIGH_FUNCTION_ENTER(
            "sleep",
            sleep_total,
            oversleep_total,
            hce::chrono::to<std::chrono::nanoseconds>(dur).count());
    size_t success_count = 0;

    auto check_overslept = [&](hce::chrono::time_point target, hce::chrono::time_point done){
        hce::chrono::duration overslept_ticks = absolute_difference(done, target);

        if(upper_bound_overslept_milli_ticks < overslept_ticks) {
            HCE_INFO_FUNCTION_BODY(
                "test::timer::sleep","[OVERSLEPT] missed target milli:", 
                hce::chrono::to<std::chrono::milliseconds>(overslept_ticks).count(), 
                ", overslept upper bound milli:", 
                hce::chrono::to<std::chrono::milliseconds>(upper_bound_overslept_milli_ticks).count());
            ++oversleep_total;
        }
    };

    {
        HCE_HIGH_FUNCTION_ENTER("sleep","thread duration");
        auto now = hce::chrono::now();
        auto requested_sleep_ticks = 
            hce::chrono::to<std::chrono::nanoseconds>(dur).count();

        hce::chrono::time_point target(dur + now);
        hce::sleep(dur);
        ++sleep_total;

        auto done = hce::chrono::now();
        auto slept_ticks = 
            hce::chrono::to<std::chrono::nanoseconds>(
                absolute_difference(done,now)).count();

        // ensure we slept at least the correct amount of time
        EXPECT_GE(slept_ticks, requested_sleep_ticks); 

        // ensure we didn't sleep past the upper bound
        check_overslept(target, done);

        ++success_count;
    }

    {
        HCE_HIGH_FUNCTION_ENTER("sleep","thread time_point");
        auto now = hce::chrono::now();
        auto requested_sleep_ticks = 
            hce::chrono::to<std::chrono::nanoseconds>(dur).count();

        hce::chrono::time_point target(dur + now);
        hce::sleep(target);
        ++sleep_total;

        auto done = hce::chrono::now();
        auto slept_ticks = 
            hce::chrono::to<std::chrono::nanoseconds>(
                absolute_difference(done,now)).count();

        // ensure we slept at least the correct amount of time
        EXPECT_GE(slept_ticks, requested_sleep_ticks); 

        // ensure we didn't sleep past the upper bound
        check_overslept(target, done);

        ++success_count;
    }

    {
        HCE_HIGH_FUNCTION_ENTER("sleep","thread sleep through timeout duration");
        auto now = hce::chrono::now();
        auto requested_sleep_ticks = 
            hce::chrono::to<std::chrono::nanoseconds>(dur).count();

        hce::chrono::time_point target(dur + now);

        {
            auto awt = hce::sleep(dur);
            ++sleep_total;

            // ensure we sleep for the entire normal 
            std::this_thread::sleep_for(dur);
            // the awaitable should return immediately
        }

        auto done = hce::chrono::now();
        auto slept_ticks = 
            hce::chrono::to<std::chrono::nanoseconds>(
                absolute_difference(done,now)).count();

        // ensure we slept at least the correct amount of time
        EXPECT_GE(slept_ticks, requested_sleep_ticks); 

        // ensure we didn't sleep past the upper bound
        check_overslept(target, done);

        ++success_count;
    }

    {
        HCE_HIGH_FUNCTION_ENTER("sleep","thread sleep through timeout time_point");
        auto now = hce::chrono::now();
        auto requested_sleep_ticks = 
            hce::chrono::to<std::chrono::nanoseconds>(dur).count();

        hce::chrono::time_point target(dur + now);

        {
            auto awt = hce::sleep(target);
            ++sleep_total;

            // ensure we sleep for the entire normal 
            std::this_thread::sleep_for(dur);
            // the awaitable should return immediately
        }

        auto done = hce::chrono::now();
        auto slept_ticks = 
            hce::chrono::to<std::chrono::nanoseconds>(
                absolute_difference(done,now)).count();

        // ensure we slept at least the correct amount of time
        EXPECT_GE(slept_ticks, requested_sleep_ticks); 

        // ensure we didn't sleep past the upper bound
        check_overslept(target, done);

        ++success_count;
    }

    {
        HCE_HIGH_FUNCTION_ENTER("sleep","stacked thread duration");
        auto now = hce::chrono::now();
        auto requested_sleep_ticks = 
            hce::chrono::to<std::chrono::nanoseconds>(dur).count();

        hce::chrono::time_point target(dur + std::chrono::milliseconds(max_timer_offset) + now);
        std::deque<hce::awt<void>> started_q;

        for(size_t c=max_timer_offset; c>0; --c) {
            started_q.push_back(hce::sleep(dur + std::chrono::milliseconds(c)));
        }

        // count all timers as 1 sleep because we only check overslept once
        ++sleep_total;

        for(size_t c=max_timer_offset; c>0; --c) {
            auto awt = std::move(started_q.front());
            started_q.pop_front();
        }

        auto done = hce::chrono::now();
        auto slept_ticks = 
            hce::chrono::to<std::chrono::nanoseconds>(
                absolute_difference(done,now)).count();

        // ensure we slept at least the correct amount of time
        EXPECT_GE(slept_ticks, requested_sleep_ticks); 

        // ensure we didn't sleep past the upper bound
        check_overslept(target, done);

        ++success_count;
    }

    {
        HCE_HIGH_FUNCTION_ENTER("sleep","stacked thread time_point");
        auto now = hce::chrono::now();
        auto requested_sleep_ticks = 
            hce::chrono::to<std::chrono::nanoseconds>(dur).count();

        hce::chrono::time_point target(dur + std::chrono::milliseconds(max_timer_offset) + now);
        std::deque<hce::awt<void>> started_q;

        for(size_t c=max_timer_offset; c>0; --c) {
            started_q.push_back(hce::sleep(now + dur + std::chrono::milliseconds(c)));
        }

        // count all timers as 1 sleep because we only check overslept once
        ++sleep_total;

        for(size_t c=max_timer_offset; c>0; --c) {
            auto awt = std::move(started_q.front());
            started_q.pop_front();
        }

        auto done = hce::chrono::now();
        auto slept_ticks = 
            hce::chrono::to<std::chrono::nanoseconds>(
                absolute_difference(done,now)).count();

        // ensure we slept at least the correct amount of time
        EXPECT_GE(slept_ticks, requested_sleep_ticks); 

        // ensure we didn't sleep past the upper bound
        check_overslept(target, done);

        ++success_count;
    }

    {
        HCE_HIGH_FUNCTION_ENTER("sleep","coroutine timer duration");
        auto now = hce::chrono::now();
        auto requested_sleep_ticks = 
            hce::chrono::to<std::chrono::nanoseconds>(dur).count();

        hce::chrono::time_point target(dur + now);
        hce::schedule(co_sleep(dur));
        ++sleep_total;

        auto done = hce::chrono::now();
        auto slept_ticks = 
            hce::chrono::to<std::chrono::nanoseconds>(
                absolute_difference(done,now)).count();

        // ensure we slept at least the correct amount of time
        EXPECT_GE(slept_ticks, requested_sleep_ticks); 

        // ensure we didn't sleep past the upper bound
        check_overslept(target, done);

        ++success_count;
    }

    {
        HCE_HIGH_FUNCTION_ENTER("sleep","coroutine timer time_point");
        auto now = hce::chrono::now();
        auto requested_sleep_ticks = 
            hce::chrono::to<std::chrono::nanoseconds>(dur).count();

        hce::chrono::time_point target(dur + now);
        hce::schedule(co_sleep(target));
        ++sleep_total;

        auto done = hce::chrono::now();
        auto slept_ticks = 
            hce::chrono::to<std::chrono::nanoseconds>(
                absolute_difference(done,now)).count();

        // ensure we slept at least the correct amount of time
        EXPECT_GE(slept_ticks, requested_sleep_ticks); 

        // ensure we didn't sleep past the upper bound
        check_overslept(target, done);

        ++success_count;
    }

    {
        HCE_HIGH_FUNCTION_ENTER("sleep","stacked coroutine duration");
        auto now = hce::chrono::now();
        auto requested_sleep_ticks = 
            hce::chrono::to<std::chrono::nanoseconds>(dur).count();

        hce::chrono::time_point target(dur + std::chrono::milliseconds(max_timer_offset) + now);
        std::deque<hce::awt<void>> started_q;

        for(size_t c=max_timer_offset; c>0; --c) {
            started_q.push_back(hce::schedule(co_sleep(dur + std::chrono::milliseconds(c))));
        }

        // count all timers as 1 sleep because we only check overslept once
        ++sleep_total;

        for(size_t c=max_timer_offset; c>0; --c) {
            auto awt = std::move(started_q.front());
            started_q.pop_front();
        }

        auto done = hce::chrono::now();
        auto slept_ticks = 
            hce::chrono::to<std::chrono::nanoseconds>(
                absolute_difference(done,now)).count();

        // ensure we slept at least the correct amount of time
        EXPECT_GE(slept_ticks, requested_sleep_ticks); 

        // ensure we didn't sleep past the upper bound
        check_overslept(target, done);

        ++success_count;
    }

    {
        HCE_HIGH_FUNCTION_ENTER("sleep","stacked coroutine time_point");
        auto now = hce::chrono::now();
        auto requested_sleep_ticks = 
            hce::chrono::to<std::chrono::nanoseconds>(dur).count();

        hce::chrono::time_point target(dur + std::chrono::milliseconds(max_timer_offset) + now);
        std::deque<hce::awt<void>> started_q;

        for(size_t c=max_timer_offset; c>0; --c) {
            started_q.push_back(hce::schedule(co_sleep(now + dur + std::chrono::milliseconds(c))));
        }

        // count all timers as 1 sleep because we only check overslept once
        ++sleep_total;

        for(size_t c=max_timer_offset; c>0; --c) {
            auto awt = std::move(started_q.front());
            started_q.pop_front();
        }

        auto done = hce::chrono::now();
        auto slept_ticks = 
            hce::chrono::to<std::chrono::nanoseconds>(
                absolute_difference(done,now)).count();

        // ensure we slept at least the correct amount of time
        EXPECT_GE(slept_ticks, requested_sleep_ticks); 

        // ensure we didn't sleep past the upper bound
        check_overslept(target, done);

        ++success_count;
    }

    HCE_HIGH_FUNCTION_ENTER("sleep","done");
    return success_count; 
}

}
}

TEST_F(timer, sleep_short) {
    size_t expected_successes = 10;

    for(unsigned int i=0; i<test::timer::iterate_limit; ++i) {
        HCE_INFO_LOG("TEST_F(timer,sleep_short) milli:%u",i);
        EXPECT_EQ(expected_successes,test::timer::sleep(std::chrono::milliseconds(i)));
        EXPECT_EQ(expected_successes,test::timer::sleep(std::chrono::microseconds(1000*i)));
        EXPECT_EQ(expected_successes,test::timer::sleep(std::chrono::nanoseconds(1000000*i)));
    }

    test::timer::validate_test({95.0},{},{25.0});
}

TEST_F(timer, sleep_medium) {
    size_t expected_successes = 10;
   
    for(unsigned int i=1; i<test::timer::medium_iterate_limit; ++i) {
        const unsigned int milli_dur = i * test::timer::milli_one_hundreth_second;
        HCE_INFO_LOG("TEST_F(timer,sleep_medium) milli:%u",milli_dur);
        EXPECT_EQ(expected_successes,test::timer::sleep(std::chrono::milliseconds(milli_dur)));
    }

    test::timer::validate_test({90.0},{},{5.0});
}

TEST_F(timer, sleep_long) {
    size_t expected_successes = 10;
   
    for(unsigned int i=1; i<test::timer::long_iterate_limit; ++i) {
        const unsigned int milli_dur = i * test::timer::milli_one_fortieth_second;
        HCE_INFO_LOG("TEST_F(timer,sleep_long) milli:%u",milli_dur);
        EXPECT_EQ(expected_successes,test::timer::sleep(std::chrono::milliseconds(milli_dur)));
    }

    test::timer::validate_test({90.0},{},{1.0});
}

namespace test {
namespace timer {

template <typename... As>
size_t cancel(As&&... as) {
    const hce::chrono::duration dur(as...);
    std::stringstream ss;
    ss << "cancel:" 
       << hce::chrono::to<std::chrono::nanoseconds>(dur).count();
    size_t success_count = 0;

    auto check_running = [&](hce::sid& s, bool expected) {
        ++running_total;

        if(expected != hce::timer::running(s)) {
            ++missedrunning_total;
        }
    };

    {
        HCE_INFO_FUNCTION_BODY("cancel","thread timer cancel duration");
        test::queue<hce::sid> q;

        std::thread sleeping_thd([&]{
            hce::sid i;
            auto now = hce::chrono::now();
            auto requested_sleep_ticks = 
                hce::chrono::to<std::chrono::nanoseconds>(dur).count();
            auto awt = hce::timer::start(i, dur);
            q.push(i);
            EXPECT_FALSE((bool)std::move(awt));

            auto done = hce::chrono::now();
            auto slept_ticks = 
                hce::chrono::to<std::chrono::nanoseconds>(
                    absolute_difference(done,now)).count();

            // ensure we slept at least the correct amount of time
            EXPECT_LT(slept_ticks, requested_sleep_ticks); 
        });

        hce::sid i = q.pop();
        check_running(i, true);
        EXPECT_TRUE(hce::timer::cancel(i));
        check_running(i, false);
        sleeping_thd.join();

        ++success_count;
    }

    {
        HCE_INFO_FUNCTION_BODY("cancel","thread timer cancel time_point");
        test::queue<hce::sid> q;

        std::thread sleeping_thd([&]{
            hce::sid i;
            auto now = hce::chrono::now();
            auto requested_sleep_ticks = 
                hce::chrono::to<std::chrono::nanoseconds>(dur).count();
            hce::chrono::time_point target(dur + now);
            auto awt = hce::timer::start(i, target);
            q.push(i);
            EXPECT_FALSE((bool)std::move(awt));

            auto done = hce::chrono::now();
            auto slept_ticks = 
                hce::chrono::to<std::chrono::nanoseconds>(
                    absolute_difference(done,now)).count();

            // ensure we slept at least the correct amount of time
            EXPECT_LT(slept_ticks, requested_sleep_ticks); 
        });

        hce::sid i = q.pop();
        check_running(i, true);
        EXPECT_TRUE(hce::timer::cancel(i));
        check_running(i, false);
        sleeping_thd.join();

        ++success_count;
    }

    {
        HCE_INFO_FUNCTION_BODY("cancel","coroutine timer cancel duration");
        test::queue<hce::sid> q;
        auto now = hce::chrono::now();
        auto requested_sleep_ticks = 
            hce::chrono::to<std::chrono::nanoseconds>(dur).count();

        auto awt = hce::schedule(co_timer(q, dur));
        hce::sid i = q.pop();
        check_running(i, true);
        EXPECT_TRUE(hce::timer::cancel(i));
        check_running(i, false);

        EXPECT_FALSE((bool)std::move(awt));

        auto done = hce::chrono::now();
        auto slept_ticks = 
            hce::chrono::to<std::chrono::nanoseconds>(
                absolute_difference(done,now)).count();

        // ensure we slept at least the correct amount of time
        EXPECT_LT(slept_ticks, requested_sleep_ticks); 

        ++success_count;
    }

    {
        HCE_INFO_FUNCTION_BODY("cancel","coroutine timer cancel time_point");
        test::queue<hce::sid> q;
        auto now = hce::chrono::now();
        auto requested_sleep_ticks = 
            hce::chrono::to<std::chrono::nanoseconds>(dur).count();

        hce::chrono::time_point target(dur + now);
        auto awt = hce::schedule(co_timer(q, target));
        hce::sid i = q.pop();
        check_running(i, true);
        EXPECT_TRUE(hce::timer::cancel(i));
        check_running(i, false);

        EXPECT_FALSE((bool)std::move(awt));

        auto done = hce::chrono::now();
        auto slept_ticks = 
            hce::chrono::to<std::chrono::nanoseconds>(
                absolute_difference(done,now)).count();

        // ensure we slept at least the correct amount of time
        EXPECT_LT(slept_ticks, requested_sleep_ticks); 

        ++success_count;
    }

    return success_count; 
}

}
}

TEST_F(timer, cancel) {
    size_t expected_successes = 4;

    for(unsigned int i=1; i<test::timer::iterate_limit; ++i) {
        // the length of time shouldn't matter because we cancel all immediately
        EXPECT_EQ(expected_successes,test::timer::cancel(std::chrono::milliseconds(i * 50)));
        EXPECT_EQ(expected_successes,test::timer::cancel(std::chrono::seconds(i * 50)));
        EXPECT_EQ(expected_successes,test::timer::cancel(std::chrono::hours(i * 50)));
    }

    test::timer::validate_test({},{98.0},{1.0});
}
