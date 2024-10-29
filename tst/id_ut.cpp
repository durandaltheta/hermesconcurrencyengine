//SPDX-License-Identifier: Apache-2.0
//Author: Blayne Dennis 
#include "id.hpp"

#include <gtest/gtest.h>
#include "test_helpers.hpp"

TEST(id, id) {
    hce::id id(new std::byte);
    hce::id id2;

    EXPECT_NE(nullptr, id.get());
    EXPECT_EQ(nullptr, id2.get());
    EXPECT_EQ(id, id);
    EXPECT_EQ(id2, id2);
    EXPECT_NE(id, id2);

    id2 = id;

    EXPECT_EQ(id, id2);
    EXPECT_NE(nullptr, id.get());
    EXPECT_NE(nullptr, id2.get());

    id.reset();
    EXPECT_NE(id, id2);
    EXPECT_EQ(nullptr, id.get());
    EXPECT_NE(nullptr, id2.get());

    id2.reset();
    EXPECT_EQ(id, id2);
    EXPECT_EQ(nullptr, id.get());
    EXPECT_EQ(nullptr, id2.get());
}
