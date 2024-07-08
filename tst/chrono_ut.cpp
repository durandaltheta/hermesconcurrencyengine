//SPDX-License-Identifier: Apache-2.0
//Author: Blayne Dennis 
#include <chrono>
#include <thread>

#include "utility.hpp"
#include "chrono.hpp"

#include <gtest/gtest.h> 
#include "test_helpers.hpp"

namespace test {
namespace chrono {

hce::chrono::duration absolute_difference(
    const hce::chrono::duration& d0, 
    const hce::chrono::duration& d1) {
    return d0 > d1 ? (d0 - d1) : (d1 - d0);
}

}
}

TEST(chrono, duration) {
    {
        auto now = hce::chrono::now();
        auto time_since_epoch = now.time_since_epoch();
        auto now_dur = hce::chrono::duration(now);

        EXPECT_EQ(time_since_epoch, now_dur);
        EXPECT_EQ(now_dur, hce::chrono::duration(now_dur));
    }

    {
        auto duration_from_unit = [](const size_t count) {
            EXPECT_EQ(hce::chrono::hours(count),hce::chrono::duration(hce::chrono::hours(count)));
            EXPECT_EQ(hce::chrono::minutes(count),hce::chrono::duration(hce::chrono::minutes(count)));
            EXPECT_EQ(hce::chrono::seconds(count),hce::chrono::duration(hce::chrono::seconds(count)));
            EXPECT_EQ(hce::chrono::milliseconds(count),hce::chrono::duration(hce::chrono::milliseconds(count)));
            EXPECT_EQ(hce::chrono::microseconds(count),hce::chrono::duration(hce::chrono::microseconds(count)));
            EXPECT_EQ(hce::chrono::nanoseconds(count),hce::chrono::duration(hce::chrono::nanoseconds(count)));
        };

        size_t count=0;
        const size_t max=10000;

        for(; count<max; ++count) {
            duration_from_unit(count);
        }

        EXPECT_EQ(count,max);
    }
}

TEST(chrono, time_point) {
    {
        auto now = hce::chrono::now();
        auto now_from_time_point = hce::chrono::time_point(now);
        auto now_from_dur = hce::chrono::time_point(hce::chrono::duration(now));

        EXPECT_EQ(now, now_from_time_point);
        EXPECT_LE(now, now_from_dur);
    }

    {
        auto time_point_from_unit = [](const size_t count) {
            // sanity
            {
                const auto now = hce::chrono::now();
                hce::chrono::time_point lhs = now + hce::chrono::hours(0);
                hce::chrono::time_point rhs = now;
                EXPECT_EQ(test::chrono::absolute_difference(lhs,rhs).to_count<hce::chrono::milliseconds>(), 0);
            }

            // hours
            {
                const auto now = hce::chrono::now();
                hce::chrono::time_point lhs = now + hce::chrono::hours(count);
                hce::chrono::time_point rhs = now + hce::chrono::duration(hce::chrono::hours(count));
                EXPECT_LE(lhs,rhs);
                // difference test is always 50 milliseconds to account for probable os time slices of no more than 20 ms
                EXPECT_LT(test::chrono::absolute_difference(lhs,rhs).to_count<hce::chrono::milliseconds>(), 50);
            }

            // minutes 
            {
                const auto now = hce::chrono::now();
                hce::chrono::time_point lhs = now + hce::chrono::minutes(count);
                hce::chrono::time_point rhs = now + hce::chrono::duration(hce::chrono::minutes(count));
                EXPECT_LE(lhs,rhs);
                EXPECT_LT(test::chrono::absolute_difference(lhs,rhs).to_count<hce::chrono::milliseconds>(), 50);
            }

            // seconds 
            {
                const auto now = hce::chrono::now();
                hce::chrono::time_point lhs = now + hce::chrono::seconds(count);
                hce::chrono::time_point rhs = now + hce::chrono::duration(hce::chrono::seconds(count));
                EXPECT_LE(lhs,rhs);
                EXPECT_LT(test::chrono::absolute_difference(lhs,rhs).to_count<hce::chrono::milliseconds>(), 50);
            }

            // milliseconds
            {
                const auto now = hce::chrono::now();
                hce::chrono::time_point lhs = now + hce::chrono::milliseconds(count);
                hce::chrono::time_point rhs = now + hce::chrono::duration(hce::chrono::milliseconds(count));
                EXPECT_LE(lhs,rhs);
                EXPECT_LT(test::chrono::absolute_difference(lhs,rhs).to_count<hce::chrono::milliseconds>(), 50);
            }

            // microseconds
            {
                const auto now = hce::chrono::now();
                hce::chrono::time_point lhs = now + hce::chrono::microseconds(count);
                hce::chrono::time_point rhs = now + hce::chrono::duration(hce::chrono::microseconds(count));
                EXPECT_LE(lhs,rhs);
                EXPECT_LT(test::chrono::absolute_difference(lhs,rhs).to_count<hce::chrono::milliseconds>(), 50);
            }

            // nanoseconds
            {
                const auto now = hce::chrono::now();
                hce::chrono::time_point lhs = now + hce::chrono::nanoseconds(count);
                hce::chrono::time_point rhs = now + hce::chrono::duration(hce::chrono::nanoseconds(count));
                EXPECT_LE(lhs,rhs);
                EXPECT_LT(test::chrono::absolute_difference(lhs,rhs).to_count<hce::chrono::milliseconds>(), 50);
            }
        };

        size_t count=0;
        const size_t max=10000;

        for(; count<max; ++count) {
            time_point_from_unit(count);
        }

        EXPECT_EQ(count,max);
    }
}
