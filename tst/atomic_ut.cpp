//SPDX-License-Identifier: Apache-2.0
//Author: Blayne Dennis 
#include "atomic.hpp"

#include <thread>
#include <chrono>

#include <gtest/gtest.h>

TEST(spinlock, construct) {
    hce::spinlock slk;
}

TEST(spinlock, lock_unlock) {
    bool written = false;
    bool tested = false;
    hce::spinlock slk;

    slk.lock();

    std::thread tester([&]{
        slk.lock();
        EXPECT_TRUE(written);
        tested = true;
        slk.unlock();
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    written = true;

    slk.unlock();

    tester.join();
    EXPECT_TRUE(tested);
}

TEST(spinlock, try_lock_unlock) {
    hce::spinlock slk;

    slk.lock();

    std::thread tester([&]{
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        slk.unlock();
    });

    for(size_t i=0; i<100; ++i) {
        EXPECT_FALSE(slk.try_lock());
    }

    tester.join();

    EXPECT_TRUE(slk.try_lock());
    slk.unlock();
    EXPECT_TRUE(slk.try_lock());
    slk.unlock();
}
