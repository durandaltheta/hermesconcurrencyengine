//SPDX-License-Identifier: Apache-2.0
//Author: Blayne Dennis 
#include "coroutine.hpp"

#include <string>

#include <gtest/gtest.h> 

TEST(coroutine, return_void) {
    struct test {
        static inline hce::coroutine<void> co() {
            co_return;
        }
    };

    {
        hce::coroutine<void> co = test::co();
        EXPECT_TRUE(co);
        EXPECT_FALSE(co.done());
        co.resume();
        EXPECT_TRUE(co.done());
    }
}
