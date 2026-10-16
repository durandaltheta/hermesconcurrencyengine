//SPDX-License-Identifier: Apache-2.0
//Author: Blayne Dennis 

#include "lifecycle.hpp"

#include <gtest/gtest.h>

TEST(lifecycle, config_logging) {
    hce::lifecycle::config c;

    EXPECT_LT(c.log.loglevel, 10);
    EXPECT_GT(c.log.loglevel, -10);
}

TEST(lifecycle, config_allocator) {
    hce::lifecycle::config c;
}

TEST(lifecycle, scheduler) {
    hce::lifecycle::config c;

    EXPECT_EQ(c.log.loglevel, c.sch.global_config.loglevel);
}

TEST(lifecycle, threadpool) {
    hce::lifecycle::config c;

    EXPECT_EQ(c.log.loglevel, c.sch.global_config.loglevel);
}
