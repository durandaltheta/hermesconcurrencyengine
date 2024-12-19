//SPDX-License-Identifier: Apache-2.0
//Author: Blayne Dennis 
#include <string>
#include <forward_list>

#include "synchronized_list.hpp"

#include <gtest/gtest.h>
#include "test_helpers.hpp"

namespace test {

template <typename T>
void emplace_back_front_pop_T() {
    hce::synchronized_list<T> q;
    T t;

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
void rvalue_push_back_front_pop_T() {
    hce::synchronized_list<T> q;
    T t;

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
void lvalue_push_back_front_pop_T() {
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
        T t;
        q.pop(t);
        EXPECT_EQ((T)test::init<T>(i), t);
        EXPECT_EQ(100 - (i+1), q.size());
    }

    EXPECT_EQ(0, q.size());
    EXPECT_TRUE(q.empty());
}

}

TEST(synchronized_queue, emplace_back_front_pop) {
    test::emplace_back_front_pop_T<int>();
    test::emplace_back_front_pop_T<unsigned int>();
    test::emplace_back_front_pop_T<size_t>();
    test::emplace_back_front_pop_T<float>();
    test::emplace_back_front_pop_T<double>();
    test::emplace_back_front_pop_T<char>();
    test::emplace_back_front_pop_T<std::string>();
    test::emplace_back_front_pop_T<test::CustomObject>();
}

TEST(synchronized_queue, rvalue_push_back_front_pop) {
    test::rvalue_push_back_front_pop_T<int>();
    test::rvalue_push_back_front_pop_T<unsigned int>();
    test::rvalue_push_back_front_pop_T<size_t>();
    test::rvalue_push_back_front_pop_T<float>();
    test::rvalue_push_back_front_pop_T<double>();
    test::rvalue_push_back_front_pop_T<char>();
    test::rvalue_push_back_front_pop_T<std::string>();
    test::rvalue_push_back_front_pop_T<test::CustomObject>();
}

TEST(synchronized_queue, lvalue_push_back_front_pop) {
    test::lvalue_push_back_front_pop_T<int>();
    test::lvalue_push_back_front_pop_T<unsigned int>();
    test::lvalue_push_back_front_pop_T<size_t>();
    test::lvalue_push_back_front_pop_T<float>();
    test::lvalue_push_back_front_pop_T<double>();
    test::lvalue_push_back_front_pop_T<char>();
    test::lvalue_push_back_front_pop_T<std::string>();
    test::lvalue_push_back_front_pop_T<test::CustomObject>();
}
