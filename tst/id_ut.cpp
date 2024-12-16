//SPDX-License-Identifier: Apache-2.0
//Author: Blayne Dennis 
#include "id.hpp"

#include <gtest/gtest.h>
#include "test_helpers.hpp"

TEST(id, uid) {
    hce::uid uid;
    hce::uid uid2;

    ASSERT_EQ(nullptr, uid.get());
    ASSERT_EQ(nullptr, uid2.get());
    ASSERT_FALSE(uid);
    ASSERT_FALSE(uid2);
    ASSERT_EQ(uid, uid2);

    uid.make();

    ASSERT_NE(nullptr, uid.get());
    ASSERT_EQ(nullptr, uid2.get());
    ASSERT_TRUE(uid);
    ASSERT_FALSE(uid2);
    ASSERT_EQ(uid, uid);
    ASSERT_EQ(uid2, uid2);
    ASSERT_NE(uid, uid2);

    uid2 = std::move(uid);

    ASSERT_NE(nullptr, uid.get());
    ASSERT_EQ(nullptr, uid.get());
    ASSERT_NE(nullptr, uid2.get());
    ASSERT_FALSE(uid);
    ASSERT_TRUE(uid2);

    uid.make();
    ASSERT_NE(uid, uid2);
    ASSERT_NE(nullptr, uid.get());
    ASSERT_NE(nullptr, uid2.get());
    ASSERT_TRUE(uid);
    ASSERT_TRUE(uid2);

    uid2.reset();
    ASSERT_NE(nullptr, uid.get());
    ASSERT_EQ(nullptr, uid2.get());
    ASSERT_TRUE(uid);
    ASSERT_FALSE(uid2);
    ASSERT_NE(uid, uid2);

    uid.reset();
    ASSERT_EQ(nullptr, uid.get());
    ASSERT_EQ(nullptr, uid2.get());
    ASSERT_FALSE(uid);
    ASSERT_FALSE(uid2);
    ASSERT_EQ(uid, uid2);
}

TEST(id, sid) {
    hce::sid sid;
    hce::sid sid2;

    ASSERT_EQ(nullptr, sid.get());
    ASSERT_EQ(nullptr, sid2.get());
    ASSERT_FALSE(sid);
    ASSERT_FALSE(sid2);
    ASSERT_EQ(sid, sid2);

    sid.make();

    ASSERT_NE(nullptr, sid.get());
    ASSERT_EQ(nullptr, sid2.get());
    ASSERT_TRUE(sid);
    ASSERT_FALSE(sid2);
    ASSERT_EQ(sid, sid);
    ASSERT_EQ(sid2, sid2);
    ASSERT_NE(sid, sid2);

    sid2 = sid;

    ASSERT_EQ(sid, sid2);
    ASSERT_NE(nullptr, sid.get());
    ASSERT_NE(nullptr, sid2.get());
    ASSERT_TRUE(sid);
    ASSERT_TRUE(sid2);

    sid.reset();
    ASSERT_NE(sid, sid2);
    ASSERT_EQ(nullptr, sid.get());
    ASSERT_NE(nullptr, sid2.get());
    ASSERT_FALSE(sid);
    ASSERT_TRUE(sid2);

    sid2.reset();
    ASSERT_EQ(nullptr, sid.get());
    ASSERT_EQ(nullptr, sid2.get());
    ASSERT_FALSE(sid);
    ASSERT_FALSE(sid2);
    ASSERT_EQ(sid, sid2);
}
