//SPDX-License-Identifier: Apache-2.0
//Author: Blayne Dennis 
#include <string>
#include <forward_list>

#include "hce/synchronized_list.hpp"

#include <gtest/gtest.h>
#include "test_helpers.hpp"

namespace {

template <typename T>
void sync_emplace_back_front_pop_T() {
    hce::synchronized_list<T> q;
    T t{};

    EXPECT_EQ(0, q.size());
    EXPECT_TRUE(q.empty());

    for(size_t i=0; i<100; ++i) {
        q.emplace_back((T)test::init<T>(i));
    }

    EXPECT_EQ(100, q.size());
    EXPECT_FALSE(q.empty());

    for(size_t i=0; i<100; ++i) {
        q.pop(t);
        EXPECT_EQ((T)test::init<T>(i), t);
        EXPECT_EQ(100 - (i+1), q.size());
    }

    EXPECT_EQ(0, q.size());
    EXPECT_TRUE(q.empty());
}

template <typename T>
void sync_rvalue_push_back_front_pop_T() {
    hce::synchronized_list<T> q;
    T t{};

    EXPECT_EQ(0, q.size());
    EXPECT_TRUE(q.empty());

    for(size_t i=0; i<100; ++i) {
        q.push_back((T)test::init<T>(i));
    }

    EXPECT_EQ(100, q.size());
    EXPECT_FALSE(q.empty());

    for(size_t i=0; i<100; ++i) {
        q.pop(t);
        EXPECT_EQ((T)test::init<T>(i), t);
        EXPECT_EQ(100 - (i+1), q.size());
    }

    EXPECT_EQ(0, q.size());
    EXPECT_TRUE(q.empty());
}

template <typename T>
void sync_lvalue_push_back_front_pop_T() {
    hce::synchronized_list<T> q;

    EXPECT_EQ(0, q.size());
    EXPECT_TRUE(q.empty());

    for(size_t i=0; i<100; ++i) {
        T t = test::init<T>(i);
        q.push_back(t);
    }

    EXPECT_EQ(100, q.size());
    EXPECT_FALSE(q.empty());

    for(size_t i=0; i<100; ++i) {
        T t{};
        q.pop(t);
        EXPECT_EQ((T)test::init<T>(i), t);
        EXPECT_EQ(100 - (i+1), q.size());
    }

    EXPECT_EQ(0, q.size());
    EXPECT_TRUE(q.empty());
}

}

TEST(synchronized_queue, emplace_back_front_pop) {
    sync_emplace_back_front_pop_T<int>();
    sync_emplace_back_front_pop_T<unsigned int>();
    sync_emplace_back_front_pop_T<size_t>();
    sync_emplace_back_front_pop_T<float>();
    sync_emplace_back_front_pop_T<double>();
    sync_emplace_back_front_pop_T<char>();
    sync_emplace_back_front_pop_T<std::string>();
    sync_emplace_back_front_pop_T<test::CustomObject>();
}

TEST(synchronized_queue, rvalue_push_back_front_pop) {
    sync_rvalue_push_back_front_pop_T<int>();
    sync_rvalue_push_back_front_pop_T<unsigned int>();
    sync_rvalue_push_back_front_pop_T<size_t>();
    sync_rvalue_push_back_front_pop_T<float>();
    sync_rvalue_push_back_front_pop_T<double>();
    sync_rvalue_push_back_front_pop_T<char>();
    sync_rvalue_push_back_front_pop_T<std::string>();
    sync_rvalue_push_back_front_pop_T<test::CustomObject>();
}

TEST(synchronized_queue, lvalue_push_back_front_pop) {
    sync_lvalue_push_back_front_pop_T<int>();
    sync_lvalue_push_back_front_pop_T<unsigned int>();
    sync_lvalue_push_back_front_pop_T<size_t>();
    sync_lvalue_push_back_front_pop_T<float>();
    sync_lvalue_push_back_front_pop_T<double>();
    sync_lvalue_push_back_front_pop_T<char>();
    sync_lvalue_push_back_front_pop_T<std::string>();
    sync_lvalue_push_back_front_pop_T<test::CustomObject>();
}
