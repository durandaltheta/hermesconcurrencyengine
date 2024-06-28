//SPDX-License-Identifier: Apache-2.0
//Author: Blayne Dennis 
#include "loguru.hpp"
#include "atomic.hpp"
#include "coroutine.hpp"
#include "scheduler.hpp"
#include "channel.hpp"

#include <gtest/gtest.h>  

namespace test {
}

TEST(channel, construct) {
    // unbuffered spinlock
    {
        hce::channel<int> ch; 
        EXPECT_FALSE(ch);
        auto ctx = std::shared_ptr<hce::channel<int>::unbuffered<hce::spinlock>>::make();
        ch.construct(ctx);
        EXPECT_TRUE(ch);
        EXPECT_EQ(ch.type_info(), typeid(hce::channel<int>::unbuffered<hce::spinlock>));
        EXPECT_EQ(ctx, ch.context());
    }

    // unbuffered lockfree
    {
        hce::channel<int> ch; 
        EXPECT_FALSE(ch);
        auto ctx = std::shared_ptr<hce::channel<int>::unbuffered<hce::lockfree>>::make();
        ch.construct(ctx);
        EXPECT_TRUE(ch);
        EXPECT_EQ(ch.type_info(), typeid(hce::channel<int>::unbuffered<hce::lockfree>));
        EXPECT_EQ(ctx, ch.context());
    }

    // buffered spinlock size 1
    {
        hce::channel<int> ch; 
        EXPECT_FALSE(ch);
        auto ctx = std::shared_ptr<hce::channel<int>::buffered<hce::spinlock>>::make(1);
        ch.construct(ctx);
        EXPECT_TRUE(ch);
        EXPECT_EQ(ch.type_info(), typeid(hce::channel<int>::buffered<hce::spinlock>));
        EXPECT_EQ(ctx, ch.context());
    }

    // buffered lockfree size 1
    {
        hce::channel<int> ch; 
        EXPECT_FALSE(ch);
        auto ctx = std::shared_ptr<hce::channel<int>::buffered<hce::lockfree>>::make(1);
        ch.construct(ctx);
        EXPECT_TRUE(ch);
        EXPECT_EQ(ch.type_info(), typeid(hce::channel<int>::buffered<hce::lockfree>));
        EXPECT_EQ(ctx, ch.context());
    }

    // buffered spinlock size 1337
    {
        hce::channel<int> ch; 
        EXPECT_FALSE(ch);
        auto ctx = std::shared_ptr<hce::channel<int>::buffered<hce::spinlock>>::make(1337);
        ch.construct(ctx);
        EXPECT_TRUE(ch);
        EXPECT_EQ(ch.type_info(), typeid(hce::channel<int>::buffered<hce::spinlock>));
        EXPECT_EQ(ctx, ch.context());
    }

    // buffered lockfree size 1337
    {
        hce::channel<int> ch; 
        EXPECT_FALSE(ch);
        auto ctx = std::shared_ptr<hce::channel<int>::buffered<hce::lockfree>>::make(1337);
        ch.construct(ctx);
        EXPECT_TRUE(ch);
        EXPECT_EQ(ch.type_info(), typeid(hce::channel<int>::buffered<hce::lockfree>));
        EXPECT_EQ(ctx, ch.context());
    }
}
