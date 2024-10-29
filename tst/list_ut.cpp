//SPDX-License-Identifier: Apache-2.0
//Author: Blayne Dennis 
#include <string>
#include <forward_list>

#include "list.hpp"

#include <gtest/gtest.h>
#include "test_helpers.hpp"

namespace test {

template <typename T>
void emplace_back_front_pop_T() {
    hce::list<T> q;

    EXPECT_EQ(0, q.size());
    EXPECT_TRUE(q.empty());

    for(size_t i=0; i<100; ++i) {
        q.emplace_back((T)test::init<T>(i));
    }

    EXPECT_EQ(100, q.size());
    EXPECT_FALSE(q.empty());

    for(size_t i=0; i<100; ++i) {
        EXPECT_EQ((T)test::init<T>(i), q.front());
        q.pop();
        EXPECT_EQ(100 - (i+1), q.size());
    }

    EXPECT_EQ(0, q.size());
    EXPECT_TRUE(q.empty());
}

template <typename T>
void rvalue_push_back_front_pop_T() {
    hce::list<T> q;

    EXPECT_EQ(0, q.size());
    EXPECT_TRUE(q.empty());

    for(size_t i=0; i<100; ++i) {
        q.push_back((T)test::init<T>(i));
    }

    EXPECT_EQ(100, q.size());
    EXPECT_FALSE(q.empty());

    for(size_t i=0; i<100; ++i) {
        EXPECT_EQ((T)test::init<T>(i), q.front());
        q.pop();
        EXPECT_EQ(100 - (i+1), q.size());
    }

    EXPECT_EQ(0, q.size());
    EXPECT_TRUE(q.empty());
}

template <typename T>
void lvalue_push_back_front_pop_T() {
    hce::list<T> q;

    EXPECT_EQ(0, q.size());
    EXPECT_TRUE(q.empty());

    for(size_t i=0; i<100; ++i) {
        T t = test::init<T>(i);
        q.push_back(t);
    }

    EXPECT_EQ(100, q.size());
    EXPECT_FALSE(q.empty());

    for(size_t i=0; i<100; ++i) {
        EXPECT_EQ((T)test::init<T>(i), q.front());
        q.pop();
        EXPECT_EQ(100 - (i+1), q.size());
    }

    EXPECT_EQ(0, q.size());
    EXPECT_TRUE(q.empty());
}

template <typename T>
void move_queue_T() {
    // constructor
    {
        hce::list<T> q;

        EXPECT_EQ(0, q.size());
        EXPECT_TRUE(q.empty());

        for(size_t i=0; i<100; ++i) {
            T t = test::init<T>(i);
            q.push_back(t);
        }

        EXPECT_EQ(100, q.size());
        EXPECT_FALSE(q.empty());

        hce::list<T> q2(std::move(q));

        EXPECT_EQ(0, q.size());
        EXPECT_TRUE(q.empty());

        EXPECT_EQ(100, q2.size());
        EXPECT_FALSE(q2.empty());

        for(size_t i=0; i<100; ++i) {
            EXPECT_EQ((T)test::init<T>(i), q2.front());
            q2.pop();
            EXPECT_EQ(100 - (i+1), q2.size());
        }

        EXPECT_EQ(0, q2.size());
        EXPECT_TRUE(q2.empty());
    }

    // operator=
    {
        hce::list<T> q;

        EXPECT_EQ(0, q.size());
        EXPECT_TRUE(q.empty());

        for(size_t i=0; i<100; ++i) {
            T t = test::init<T>(i);
            q.push_back(t);
        }

        EXPECT_EQ(100, q.size());
        EXPECT_FALSE(q.empty());

        hce::list<T> q2 = std::move(q);

        EXPECT_EQ(0, q.size());
        EXPECT_TRUE(q.empty());

        EXPECT_EQ(100, q2.size());
        EXPECT_FALSE(q2.empty());

        for(size_t i=0; i<100; ++i) {
            EXPECT_EQ((T)test::init<T>(i), q2.front());
            q2.pop();
            EXPECT_EQ(100 - (i+1), q2.size());
        }

        EXPECT_EQ(0, q2.size());
        EXPECT_TRUE(q2.empty());
    }
}

template <typename T>
void copy_queue_T() {
    // constructor
    {
        hce::list<T> q;

        EXPECT_EQ(0, q.size());
        EXPECT_TRUE(q.empty());

        for(size_t i=0; i<100; ++i) {
            T t = test::init<T>(i);
            q.push_back(t);
            EXPECT_EQ(i+1, q.size());
        }

        EXPECT_EQ(100, q.size());
        EXPECT_FALSE(q.empty());

        hce::list<T> q2(q);

        EXPECT_EQ(100, q.size());
        EXPECT_FALSE(q.empty());

        EXPECT_EQ(100, q2.size());
        EXPECT_FALSE(q2.empty());

        for(size_t i=0; i<100; ++i) {
            EXPECT_EQ((T)test::init<T>(i), q.front());
            q.pop();
            EXPECT_EQ(100 - (i+1), q.size());
        }

        EXPECT_EQ(0, q.size());
        EXPECT_TRUE(q.empty());

        EXPECT_EQ(100, q2.size());
        EXPECT_FALSE(q2.empty());

        for(size_t i=0; i<100; ++i) {
            EXPECT_EQ((T)test::init<T>(i), q2.front());
            q2.pop();
            EXPECT_EQ(100 - (i+1), q2.size());
        }

        EXPECT_EQ(0, q2.size());
        EXPECT_TRUE(q2.empty());
    }

    // operator=
    {
        hce::list<T> q;

        EXPECT_EQ(0, q.size());
        EXPECT_TRUE(q.empty());

        for(size_t i=0; i<100; ++i) {
            T t = test::init<T>(i);
            q.push_back(t);
            EXPECT_EQ(i+1, q.size());
        }

        EXPECT_EQ(100, q.size());
        EXPECT_FALSE(q.empty());

        hce::list<T> q2 = q;

        EXPECT_EQ(100, q.size());
        EXPECT_FALSE(q.empty());

        EXPECT_EQ(100, q2.size());
        EXPECT_FALSE(q2.empty());

        for(size_t i=0; i<100; ++i) {
            EXPECT_EQ((T)test::init<T>(i), q.front());
            q.pop();
            EXPECT_EQ(100 - (i+1), q.size());
        }

        EXPECT_EQ(0, q.size());
        EXPECT_TRUE(q.empty());

        EXPECT_EQ(100, q2.size());
        EXPECT_FALSE(q2.empty());

        for(size_t i=0; i<100; ++i) {
            EXPECT_EQ((T)test::init<T>(i), q2.front());
            q2.pop();
            EXPECT_EQ(100 - (i+1), q2.size());
        }

        EXPECT_EQ(0, q2.size());
        EXPECT_TRUE(q2.empty());
    }
}

template <typename T>
void concatenate_queue_T() {
    {
        hce::list<T> q;
        hce::list<T> q2;

        EXPECT_EQ(0, q.size());
        EXPECT_TRUE(q.empty());
        EXPECT_EQ(0, q2.size());
        EXPECT_TRUE(q2.empty());

        // both empty doesn't error
        q2.concatenate(q);
    }

    {
        hce::list<T> q;
        hce::list<T> q2;

        EXPECT_EQ(0, q.size());
        EXPECT_TRUE(q.empty());
        EXPECT_EQ(0, q2.size());
        EXPECT_TRUE(q2.empty());

        q2.push_back((T)test::init<T>(0));

        // empty rhs doesn't error
        q2.concatenate(q);

        EXPECT_EQ(0, q.size());
        EXPECT_TRUE(q.empty());
        EXPECT_EQ(1, q2.size());
        EXPECT_FALSE(q2.empty());
    }

    {
        hce::list<T> q;
        hce::list<T> q2;

        EXPECT_EQ(0, q.size());
        EXPECT_TRUE(q.empty());
        EXPECT_EQ(0, q2.size());
        EXPECT_TRUE(q2.empty());

        q.push_back((T)test::init<T>(0));

        // lhs empty doesn't error
        q2.concatenate(q);

        EXPECT_EQ(0, q.size());
        EXPECT_TRUE(q.empty());
        EXPECT_EQ(1, q2.size());
        EXPECT_FALSE(q2.empty());
    }

    {
        hce::list<T> q;
        hce::list<T> q2;

        EXPECT_EQ(0, q.size());
        EXPECT_TRUE(q.empty());
        EXPECT_EQ(0, q2.size());
        EXPECT_TRUE(q2.empty());

        EXPECT_EQ(0, q.size());
        EXPECT_TRUE(q.empty());
        EXPECT_EQ(0, q2.size());
        EXPECT_TRUE(q2.empty());

        for(size_t i=0; i<100; ++i) {
            T t = test::init<T>(i);
            q.push_back(t);
        }

        EXPECT_EQ(100, q.size());
        EXPECT_FALSE(q.empty());

        q2.emplace_back((T)test::init<T>(101));
        EXPECT_EQ(1, q2.size());
        EXPECT_FALSE(q2.empty());

        q2.concatenate(q);

        EXPECT_EQ(0, q.size());
        EXPECT_TRUE(q.empty());
        EXPECT_EQ(101, q2.size());
        EXPECT_FALSE(q2.empty());

        // ensure q is still valid
        for(size_t i=0; i<100; ++i) {
            T t = test::init<T>(i);
            q.push_back(t);
        }

        EXPECT_EQ(100, q.size());
        EXPECT_FALSE(q.empty());

        q2.concatenate(q);

        EXPECT_EQ(0, q.size());
        EXPECT_TRUE(q.empty());
        EXPECT_EQ(201, q2.size());
        EXPECT_FALSE(q2.empty());

        EXPECT_EQ((T)test::init<T>(101), q2.front());
        q2.pop();
        EXPECT_EQ(200, q2.size());
        EXPECT_FALSE(q2.empty());

        for(size_t i=0; i<100; ++i) {
            EXPECT_EQ((T)test::init<T>(i), q2.front());
            q2.pop();
            EXPECT_EQ((100 - (i+1)) + 100, q2.size());
        }

        for(size_t i=0; i<100; ++i) {
            EXPECT_EQ((T)test::init<T>(i), q2.front());
            q2.pop();
            EXPECT_EQ(100 - (i+1), q2.size());
        }

        EXPECT_EQ(0, q2.size());
        EXPECT_TRUE(q2.empty());
    }
}

}

TEST(queue, emplace_back_front_pop) {
    test::emplace_back_front_pop_T<int>();
    test::emplace_back_front_pop_T<unsigned int>();
    test::emplace_back_front_pop_T<size_t>();
    test::emplace_back_front_pop_T<float>();
    test::emplace_back_front_pop_T<double>();
    test::emplace_back_front_pop_T<char>();
    test::emplace_back_front_pop_T<std::string>();
    test::emplace_back_front_pop_T<test::CustomObject>();
}

TEST(queue, rvalue_push_back_front_pop) {
    test::rvalue_push_back_front_pop_T<int>();
    test::rvalue_push_back_front_pop_T<unsigned int>();
    test::rvalue_push_back_front_pop_T<size_t>();
    test::rvalue_push_back_front_pop_T<float>();
    test::rvalue_push_back_front_pop_T<double>();
    test::rvalue_push_back_front_pop_T<char>();
    test::rvalue_push_back_front_pop_T<std::string>();
    test::rvalue_push_back_front_pop_T<test::CustomObject>();
}

TEST(queue, lvalue_push_back_front_pop) {
    test::lvalue_push_back_front_pop_T<int>();
    test::lvalue_push_back_front_pop_T<unsigned int>();
    test::lvalue_push_back_front_pop_T<size_t>();
    test::lvalue_push_back_front_pop_T<float>();
    test::lvalue_push_back_front_pop_T<double>();
    test::lvalue_push_back_front_pop_T<char>();
    test::lvalue_push_back_front_pop_T<std::string>();
    test::lvalue_push_back_front_pop_T<test::CustomObject>();
}

TEST(queue, move_queue) {
    test::move_queue_T<int>();
    test::move_queue_T<unsigned int>();
    test::move_queue_T<size_t>();
    test::move_queue_T<float>();
    test::move_queue_T<double>();
    test::move_queue_T<char>();
    test::move_queue_T<std::string>();
    test::move_queue_T<test::CustomObject>();
}

TEST(queue, copy_queue) {
    test::copy_queue_T<int>();
    test::copy_queue_T<unsigned int>();
    test::copy_queue_T<size_t>();
    test::copy_queue_T<float>();
    test::copy_queue_T<double>();
    test::copy_queue_T<char>();
    test::copy_queue_T<std::string>();
    test::copy_queue_T<test::CustomObject>();
}

TEST(queue, concatenate_queue) {
    test::concatenate_queue_T<int>();
    test::concatenate_queue_T<unsigned int>();
    test::concatenate_queue_T<size_t>();
    test::concatenate_queue_T<float>();
    test::concatenate_queue_T<double>();
    test::concatenate_queue_T<char>();
    test::concatenate_queue_T<std::string>();
    test::concatenate_queue_T<test::CustomObject>();
}
