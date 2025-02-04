//SPDX-License-Identifier: Apache-2.0
//Author: Blayne Dennis 

#include "lifecycle.hpp"

#include <gtest/gtest.h>

TEST(lifecycle, config_logging) {
    hce::lifecycle::config c;

    EXPECT_LT(c.log.loglevel, 10);
    EXPECT_GT(c.log.loglevel, -10);
}

TEST(lifecycle, config_memory) {
    hce::lifecycle::config c;

    EXPECT_NE(nullptr, c.mem.system);
    EXPECT_NE(nullptr, c.mem.global);
    EXPECT_NE(nullptr, c.mem.scheduler);
    EXPECT_NE(c.mem.system, c.mem.global);
    EXPECT_NE(c.mem.system, c.mem.scheduler);
    EXPECT_NE(c.mem.global, c.mem.scheduler);
}

TEST(lifecycle, config_allocator) {
    hce::lifecycle::config c;
}

TEST(lifecycle, scheduler) {
    hce::lifecycle::config c;

    EXPECT_EQ(c.log.loglevel, c.sch.global_config.loglevel);
    EXPECT_EQ(c.mem.global, c.sch.global_config.cache_info);
}

TEST(lifecycle, threadpool) {
    hce::lifecycle::config c;

    EXPECT_EQ(c.log.loglevel, c.sch.global_config.loglevel);
    EXPECT_EQ(c.mem.global, c.sch.global_config.cache_info);
}
