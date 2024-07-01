//SPDX-License-Identifier: Apache-2.0
//Author: Blayne Dennis 
#include "loguru.hpp"
#include "atomic.hpp"
#include "coroutine.hpp"
#include "scheduler.hpp"
#include "channel.hpp"

#include <gtest/gtest.h>  

TEST(channel, construct) {
    // unbuffered spinlock
    {
        hce::channel<int> ch; 
        EXPECT_FALSE(ch);
        auto ctx = std::make_shared<hce::channel<int>::unbuffered<hce::spinlock>>();
        ch.construct(ctx);
        EXPECT_TRUE(ch);
        EXPECT_EQ(ch.type_info(), typeid(hce::channel<int>::unbuffered<hce::spinlock>));
        EXPECT_EQ(ctx, ch.context());

        ch.construct();
        EXPECT_EQ(ch.type_info(), typeid(hce::channel<int>::unbuffered<hce::spinlock>));
        EXPECT_NE(ctx, ch.context());

        auto ctx2 = ch.context();
        ch.construct<hce::spinlock>();
        EXPECT_EQ(ch.type_info(), typeid(hce::channel<int>::unbuffered<hce::spinlock>));
        EXPECT_NE(ctx, ch.context());
        EXPECT_NE(ctx2, ch.context());
    }

    // unbuffered lockfree
    {
        hce::channel<int> ch; 
        EXPECT_FALSE(ch);
        auto ctx = std::make_shared<hce::channel<int>::unbuffered<hce::lockfree>>();
        ch.construct(ctx);
        EXPECT_TRUE(ch);
        EXPECT_EQ(ch.type_info(), typeid(hce::channel<int>::unbuffered<hce::lockfree>));
        EXPECT_EQ(ctx, ch.context());

        ch.construct<hce::lockfree>();
        EXPECT_EQ(ch.type_info(), typeid(hce::channel<int>::unbuffered<hce::lockfree>));
        EXPECT_NE(ctx, ch.context());
    }

    // buffered spinlock size 1
    {
        hce::channel<int> ch; 
        EXPECT_FALSE(ch);
        auto ctx = std::make_shared<hce::channel<int>::buffered<hce::spinlock>>(1);
        ch.construct(ctx);
        EXPECT_TRUE(ch);
        EXPECT_EQ(ch.type_info(), typeid(hce::channel<int>::buffered<hce::spinlock>));
        EXPECT_EQ(ctx, ch.context());

        ch.construct(1);
        EXPECT_EQ(ch.type_info(), typeid(hce::channel<int>::buffered<hce::spinlock>));
        EXPECT_NE(ctx, ch.context());

        auto ctx2 = ch.context();
        ch.construct<hce::spinlock>(1);
        EXPECT_EQ(ch.type_info(), typeid(hce::channel<int>::buffered<hce::spinlock>));
        EXPECT_NE(ctx, ch.context());
        EXPECT_NE(ctx2, ch.context());
    }

    // buffered lockfree size 1
    {
        hce::channel<int> ch; 
        EXPECT_FALSE(ch);
        auto ctx = std::make_shared<hce::channel<int>::buffered<hce::lockfree>>(1);
        ch.construct(ctx);
        EXPECT_TRUE(ch);
        EXPECT_EQ(ch.type_info(), typeid(hce::channel<int>::buffered<hce::lockfree>));
        EXPECT_EQ(ctx, ch.context());

        ch.construct<hce::lockfree>(1);
        EXPECT_EQ(ch.type_info(), typeid(hce::channel<int>::buffered<hce::lockfree>));
        EXPECT_NE(ctx, ch.context());
    }
}

TEST(channel, make) {
    // unbuffered spinlock
    {
        hce::channel<int> ch; 
        EXPECT_FALSE(ch);
        ch = hce::channel<int>::make();
        EXPECT_TRUE(ch);
        EXPECT_EQ(ch.type_info(), typeid(hce::channel<int>::unbuffered<hce::spinlock>));

        ch = hce::channel<int>::make<hce::spinlock>();
        EXPECT_TRUE(ch);
        EXPECT_EQ(ch.type_info(), typeid(hce::channel<int>::unbuffered<hce::spinlock>));
    }

    // unbuffered lockfree
    {
        hce::channel<int> ch; 
        EXPECT_FALSE(ch);
        ch = hce::channel<int>::make<hce::lockfree>();
        EXPECT_TRUE(ch);
        EXPECT_EQ(ch.type_info(), typeid(hce::channel<int>::unbuffered<hce::lockfree>));
    }

    // buffered spinlock size 1
    {
        hce::channel<int> ch; 
        EXPECT_FALSE(ch);
        ch = hce::channel<int>::make(1);
        EXPECT_TRUE(ch);
        EXPECT_EQ(ch.type_info(), typeid(hce::channel<int>::buffered<hce::spinlock>));

        ch = hce::channel<int>::make<hce::spinlock>(1);
        EXPECT_TRUE(ch);
        EXPECT_EQ(ch.type_info(), typeid(hce::channel<int>::buffered<hce::spinlock>));
    }

    // buffered lockfree size 1
    {
        hce::channel<int> ch; 
        EXPECT_FALSE(ch);
        ch = hce::channel<int>::make<hce::lockfree>(1);
        EXPECT_TRUE(ch);
        EXPECT_EQ(ch.type_info(), typeid(hce::channel<int>::buffered<hce::lockfree>));
    }
}
