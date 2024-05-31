//SPDX-License-Identifier: Apache-2.0
//Author: Blayne Dennis 
#include "circular_buffer.hpp"

#include <string>

#include <gtest/gtest.h>

TEST(circular_buffer, construct_introspect) {
    {
        hce::circular_buffer<int> cb;
        EXPECT_EQ(1, cb.capacity());
        EXPECT_EQ(0, cb.size());
        EXPECT_EQ(1, cb.remaining());
        EXPECT_TRUE(cb.empty());
        EXPECT_FALSE(cb.full());
    }

    {
        hce::circular_buffer<int> cb(1);
        EXPECT_EQ(1, cb.capacity());
        EXPECT_EQ(0, cb.size());
        EXPECT_EQ(1, cb.remaining());
        EXPECT_TRUE(cb.empty());
        EXPECT_FALSE(cb.full());
    }

    {
        hce::circular_buffer<int> cb(2);
        EXPECT_EQ(2, cb.capacity());
        EXPECT_EQ(0, cb.size());
        EXPECT_EQ(2, cb.remaining());
        EXPECT_TRUE(cb.empty());
        EXPECT_FALSE(cb.full());
    }

    {
        hce::circular_buffer<int> cb(10);
        EXPECT_EQ(10, cb.capacity());
        EXPECT_EQ(0, cb.size());
        EXPECT_EQ(10, cb.remaining());
        EXPECT_TRUE(cb.empty());
        EXPECT_FALSE(cb.full());
    }

    {
        hce::circular_buffer<int> cb(100);
        EXPECT_EQ(100, cb.capacity());
        EXPECT_EQ(0, cb.size());
        EXPECT_EQ(100, cb.remaining());
        EXPECT_TRUE(cb.empty());
        EXPECT_FALSE(cb.full());
    }
}

TEST(circular_buffer, push_pop_int) {
    {
        hce::circular_buffer<int> cb;
        EXPECT_EQ(1, cb.capacity());
        EXPECT_EQ(0, cb.size());
        EXPECT_EQ(1, cb.remaining());
        EXPECT_TRUE(cb.empty());
        EXPECT_FALSE(cb.full());

        cb.push(3);

        EXPECT_EQ(1, cb.capacity());
        EXPECT_EQ(1, cb.size());
        EXPECT_EQ(0, cb.remaining());
        EXPECT_EQ(3, cb.front());
        EXPECT_FALSE(cb.empty());
        EXPECT_TRUE(cb.full());

        bool failed_push = false;

        try {
            cb.push(4);
        } catch(const hce::circular_buffer<int>::push_on_full& e) {
            failed_push = true;
        }

        EXPECT_TRUE(failed_push);
    }

    {
        hce::circular_buffer<int> cb(10);
        EXPECT_EQ(10, cb.capacity());
        EXPECT_EQ(0, cb.size());
        EXPECT_EQ(10, cb.remaining());
        EXPECT_TRUE(cb.empty());
        EXPECT_FALSE(cb.full());

        cb.push(3);

        EXPECT_EQ(10, cb.capacity());
        EXPECT_EQ(1, cb.size());
        EXPECT_EQ(9, cb.remaining());
        EXPECT_EQ(3, cb.front());
        EXPECT_FALSE(cb.empty());
        EXPECT_FALSE(cb.full());

        bool failed_push = false;

        try {
            cb.push(4);
        } catch(const hce::circular_buffer<int>::push_on_full& e) {
            failed_push = true;
        }

        EXPECT_FALSE(failed_push);

        EXPECT_EQ(3, cb.front());
        cb.pop();
        cb.pop();

        EXPECT_EQ(10, cb.capacity());
        EXPECT_EQ(0, cb.size());
        EXPECT_EQ(10, cb.remaining());
        EXPECT_TRUE(cb.empty());
        EXPECT_FALSE(cb.full());

        bool failed_front = false;

        try {
            cb.front();
        } catch(const hce::circular_buffer<int>::front_on_empty& e) {
            failed_front = true;
        }

        EXPECT_TRUE(failed_front);

        bool failed_pop = false;

        try {
            cb.pop();
        } catch(const hce::circular_buffer<int>::pop_on_empty& e) {
            failed_pop = true;
        }

        EXPECT_TRUE(failed_pop);
    }

    {
        hce::circular_buffer<int> cb(5);
        EXPECT_EQ(5, cb.capacity());
        EXPECT_EQ(0, cb.size());
        EXPECT_EQ(5, cb.remaining());
        EXPECT_TRUE(cb.empty());
        EXPECT_FALSE(cb.full());

        bool failed = false;

        try {
            for(size_t i = 0; i<cb.capacity(); ++i) {
                cb.push(i);
            }
        } catch(...) {
            failed = true;
        }

        EXPECT_FALSE(failed);
        EXPECT_EQ(5, cb.capacity());
        EXPECT_EQ(5, cb.size());
        EXPECT_EQ(0, cb.remaining());
        EXPECT_FALSE(cb.empty());
        EXPECT_TRUE(cb.full());

        failed = false;

        try {
            for(size_t i = 0; i<cb.capacity(); ++i) {
                EXPECT_EQ(i, cb.front());
                cb.pop();
            }
        } catch(...) {
            failed = true;
        }

        EXPECT_FALSE(failed);
        EXPECT_EQ(5, cb.capacity());
        EXPECT_EQ(0, cb.size());
        EXPECT_EQ(5, cb.remaining());
        EXPECT_TRUE(cb.empty());
        EXPECT_FALSE(cb.full());
    }
}

TEST(circular_buffer, push_pop_string) {
    {
        hce::circular_buffer<std::string> cb;
        EXPECT_EQ(1, cb.capacity());
        EXPECT_EQ(0, cb.size());
        EXPECT_EQ(1, cb.remaining());
        EXPECT_TRUE(cb.empty());
        EXPECT_FALSE(cb.full());

        cb.push("3");

        EXPECT_EQ(1, cb.capacity());
        EXPECT_EQ(1, cb.size());
        EXPECT_EQ(0, cb.remaining());
        EXPECT_EQ("3", cb.front());
        EXPECT_FALSE(cb.empty());
        EXPECT_TRUE(cb.full());

        bool failed_push = false;

        try {
            cb.push("4");
        } catch(const hce::circular_buffer<std::string>::push_on_full& e) {
            failed_push = true;
        }

        EXPECT_TRUE(failed_push);
    }

    {
        hce::circular_buffer<std::string> cb(10);
        EXPECT_EQ(10, cb.capacity());
        EXPECT_EQ(0, cb.size());
        EXPECT_EQ(10, cb.remaining());
        EXPECT_TRUE(cb.empty());
        EXPECT_FALSE(cb.full());

        cb.push("3");

        EXPECT_EQ(10, cb.capacity());
        EXPECT_EQ(1, cb.size());
        EXPECT_EQ(9, cb.remaining());
        EXPECT_EQ("3", cb.front());
        EXPECT_FALSE(cb.empty());
        EXPECT_FALSE(cb.full());

        bool failed_push = false;

        try {
            cb.push("4");
        } catch(const hce::circular_buffer<std::string>::push_on_full& e) {
            failed_push = true;
        }

        EXPECT_FALSE(failed_push);

        EXPECT_EQ("3", cb.front());
        cb.pop();
        cb.pop();

        EXPECT_EQ(10, cb.capacity());
        EXPECT_EQ(0, cb.size());
        EXPECT_EQ(10, cb.remaining());
        EXPECT_TRUE(cb.empty());
        EXPECT_FALSE(cb.full());

        bool failed_front = false;

        try {
            cb.front();
        } catch(const hce::circular_buffer<std::string>::front_on_empty& e) {
            failed_front = true;
        }

        EXPECT_TRUE(failed_front);

        bool failed_pop = false;

        try {
            cb.pop();
        } catch(const hce::circular_buffer<std::string>::pop_on_empty& e) {
            failed_pop = true;
        }

        EXPECT_TRUE(failed_pop);
    }

    {
        hce::circular_buffer<std::string> cb(5);
        EXPECT_EQ(5, cb.capacity());
        EXPECT_EQ(0, cb.size());
        EXPECT_EQ(5, cb.remaining());
        EXPECT_TRUE(cb.empty());
        EXPECT_FALSE(cb.full());

        bool failed = false;

        try {
            for(size_t i = 0; i<cb.capacity(); ++i) {
                cb.push(std::to_string(i));
            }
        } catch(...) {
            failed = true;
        }

        EXPECT_FALSE(failed);
        EXPECT_EQ(5, cb.capacity());
        EXPECT_EQ(5, cb.size());
        EXPECT_EQ(0, cb.remaining());
        EXPECT_FALSE(cb.empty());
        EXPECT_TRUE(cb.full());

        failed = false;

        try {
            for(size_t i = 0; i<cb.capacity(); ++i) {
                EXPECT_EQ(std::to_string(i), cb.front());
                cb.pop();
            }
        } catch(...) {
            failed = true;
        }

        EXPECT_FALSE(failed);
        EXPECT_EQ(5, cb.capacity());
        EXPECT_EQ(0, cb.size());
        EXPECT_EQ(5, cb.remaining());
        EXPECT_TRUE(cb.empty());
        EXPECT_FALSE(cb.full());
    }
}

TEST(circular_buffer, fill_and_empty_repeatedly) {
    const size_t buf_sz = 100;

    {
        hce::circular_buffer<int> cb(buf_sz);

        size_t repeat = 0;

        for(; repeat<1000; ++repeat) {
            for(size_t i=0; i<cb.capacity(); ++i) {
                cb.push(i);
            }
            
            EXPECT_EQ(100, cb.size());

            for(size_t i=0; i<cb.capacity(); ++i) {
                cb.pop();
            }

            EXPECT_EQ(0, cb.size());
        }

        EXPECT_EQ(1000, repeat);
    }

    {
        hce::circular_buffer<std::string> cb(buf_sz);

        size_t repeat = 0;

        for(; repeat<1000; ++repeat) {
            for(size_t i=0; i<cb.capacity(); ++i) {
                cb.push(std::to_string(i));
            }
            
            EXPECT_EQ(100, cb.size());

            for(size_t i=0; i<cb.capacity(); ++i) {
                cb.pop();
            }

            EXPECT_EQ(0, cb.size());
        }

        EXPECT_EQ(1000, repeat);
    }
}

TEST(circular_buffer, ordering) {
    {
        hce::circular_buffer<int> cb(3);

        cb.push(1);
        cb.push(2);
        cb.push(3);

        EXPECT_EQ(1,cb.front());
        cb.pop();
        cb.push(4);
        EXPECT_EQ(2,cb.front());
        cb.pop();
        cb.push(5);
        EXPECT_EQ(3,cb.front());
        cb.pop();
        EXPECT_EQ(4,cb.front());
        cb.pop();
        EXPECT_EQ(5,cb.front());
        cb.pop();
    }

    {
        hce::circular_buffer<std::string> cb(3);

        cb.push("1");
        cb.push("2");
        cb.push("3");

        EXPECT_EQ("1",cb.front());
        cb.pop();
        cb.push("4");
        EXPECT_EQ("2",cb.front());
        cb.pop();
        cb.push("5");
        EXPECT_EQ("3",cb.front());
        cb.pop();
        EXPECT_EQ("4",cb.front());
        cb.pop();
        EXPECT_EQ("5",cb.front());
        cb.pop();
    }
}
