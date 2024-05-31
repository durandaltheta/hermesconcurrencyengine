//SPDX-License-Identifier: Apache-2.0
//Author: Blayne Dennis 
#include "loguru.hpp"
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

namespace test {
namespace coroutine {

template <typename T>
inline hce::coroutine<T> co(T* t) {
    co_return *t;
}
}
}

TEST(coroutine, return_value) {
    {
        int i = 3;
        hce::coroutine<int> co = test::coroutine::co(&i);
        EXPECT_TRUE(co);
        EXPECT_FALSE(co.done());
        co.resume();
        EXPECT_TRUE(co.done());
        EXPECT_EQ(3, co.promise().result);
        EXPECT_NE(&i, &co.promise().result);
    }

    {
        std::string s = "3";
        hce::coroutine<std::string> co = test::coroutine::co(&s);
        EXPECT_TRUE(co);
        EXPECT_FALSE(co.done());
        co.resume();
        EXPECT_TRUE(co.done());
        EXPECT_EQ("3", co.promise().result);
        EXPECT_NE(&s, &co.promise().result);
    }
}
