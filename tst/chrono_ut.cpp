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



}
}

TEST(chrono, to_duration) {
    {
        auto now = hce::chrono::now();
        auto time_since_epoch = now.time_since_epoch();
        auto now_dur = hce::chrono::to_duration(now);

        EXPECT_EQ(time_since_epoch, now_dur);
        EXPECT_EQ(now_dur, to_duration(now_dur));
    }

    {
        auto to_duration_from_unit = [](const size_t count) {
            EXPECT_EQ(std::chrono::hours(count),hce::chrono::to_duration(hce::chrono::unit::hours,count));
            EXPECT_EQ(std::chrono::minutes(count),hce::chrono::to_duration(hce::chrono::unit::minutes,count));
            EXPECT_EQ(std::chrono::seconds(count),hce::chrono::to_duration(hce::chrono::unit::seconds,count));
            EXPECT_EQ(std::chrono::milliseconds(count),hce::chrono::to_duration(hce::chrono::unit::milliseconds,count));
            EXPECT_EQ(std::chrono::microseconds(count),hce::chrono::to_duration(hce::chrono::unit::microseconds,count));
            EXPECT_EQ(std::chrono::nanoseconds(count),hce::chrono::to_duration(hce::chrono::unit::nanoseconds,count));
        };

        size_t count=0;
        const size_t max=10000;

        for(; count<max; ++count) {
            to_duration_from_unit(count);
        }

        EXPECT_EQ(count,max);
    }
}

TEST(chrono, to_time_point) {
    {
        auto now = hce::chrono::now();
        auto now_from_to_time_point = hce::chrono::to_time_point(now);
        auto now_from_dur = hce::chrono::to_time_point(hce::chrono::to_duration(hce::chrono::unit::hours,0));

        EXPECT_EQ(now, now_from_to_time_point);
        EXPECT_LE(now, now_from_dur);
    }

    {
        auto to_time_point_from_unit = [](const size_t count) {
            auto now = hce::chrono::now();
            EXPECT_LE(now + std::chrono::hours(count),hce::chrono::to_time_point(hce::chrono::unit::hours,count));
            EXPECT_LE(now + std::chrono::minutes(count),hce::chrono::to_time_point(hce::chrono::unit::minutes,count));
            EXPECT_LE(now + std::chrono::seconds(count),hce::chrono::to_time_point(hce::chrono::unit::seconds,count));
            EXPECT_LE(now + std::chrono::milliseconds(count),hce::chrono::to_time_point(hce::chrono::unit::milliseconds,count));
            EXPECT_LE(now + std::chrono::microseconds(count),hce::chrono::to_time_point(hce::chrono::unit::microseconds,count));
            EXPECT_LE(now + std::chrono::nanoseconds(count),hce::chrono::to_time_point(hce::chrono::unit::nanoseconds,count));
        };

        size_t count=0;
        const size_t max=10000;

        for(; count<max; ++count) {
            to_time_point_from_unit(count);
        }

        EXPECT_EQ(count,max);
    }
}
