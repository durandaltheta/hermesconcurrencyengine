//SPDX-License-Identifier: Apache-2.0
//Author: Blayne Dennis 
#include <string>
#include <list>

#include "hce/alloc.hpp"

#include <gtest/gtest.h>
#include "test_helpers.hpp"

namespace {

template <typename T> 
void allocate_deallocate_T(){
    hce::pool_allocator<T> pa;

    for(size_t i=0; i<100; ++i) {
        T* t = pa.allocate(1);
        EXPECT_NE(nullptr, t);
        new(t) T((T)(test::init<T>(i)));
        EXPECT_EQ((T)test::init<T>(i),*t);
        pa.deallocate(t, 1);
    }
}

template <typename T> 
void introspect_pool_T() {
    // caching in pool works
    for(size_t i=0; i<100; ++i) {
        hce::pool_allocator<T> pa(i);
        std::list<T*> ptrs;

        EXPECT_EQ(0, pa.available());

        for(size_t u=0; u<i; ++u) {
            T* t = pa.allocate(1);
            ptrs.push_front(t);
        }

        EXPECT_EQ(0, pa.available());

        for(size_t u=0; u<i; ++u) {
            pa.deallocate(ptrs.front(), 1);
            ptrs.pop_front();

            // deallocated value is actually pushed onto the cache
            EXPECT_EQ(u+1, pa.available());
        }

        EXPECT_EQ(i, pa.available());

        for(size_t u=0; u<i; ++u) {
            T* t = pa.allocate(1);
            ptrs.push_front(t);
        }

        EXPECT_EQ(0, pa.available());

        for(size_t u=0; u<i; ++u) {
            pa.deallocate(ptrs.front(), 1);
            ptrs.pop_front();

            // deallocated value is actually pushed onto the cache again
            EXPECT_EQ(u+1, pa.available());
        }

        EXPECT_EQ(i, pa.available());
    }

    // ensure arrays don't cache
    {
        size_t i=100;
        hce::pool_allocator<T> pa(i);
        std::list<T*> ptrs;

        for(size_t u=0; u<i; ++u) {
            T* t = pa.allocate(2);
            ptrs.push_front(t);

            EXPECT_EQ(0, pa.available());
        }

        for(size_t u=0; u<i; ++u) {
            pa.deallocate(ptrs.front(), 2);
            ptrs.pop_front();

            // pool size doesn't grow
            EXPECT_EQ(0, pa.available());
        }
    }
   
    // pool growth is predictable
    for(size_t i=0; i<100; ++i) {
        hce::pool_allocator<T> pa(i);
        std::list<T*> ptrs;

        // Allocate and deallocate twice as many as the cache can hold. First
        // allocate a pool's worth of pointers.
        for(size_t u=0; u < i*2; ++u) {
            T* t = pa.allocate(1);
            ptrs.push_front(t);
        }

        // deallocate half
        for(size_t u=0; u<i; ++u) {
            pa.deallocate(ptrs.front(), 1);
            ptrs.pop_front();

            // deallocated value is actually pushed onto the cache
            EXPECT_EQ(u+1, pa.available());
        }

        for(size_t u=0; u<i; ++u) {
            pa.deallocate(ptrs.front(), 1);
            ptrs.pop_front();

            // additional deallocations are not pushed on the cache
            EXPECT_EQ(i, pa.available());
        }

        for(size_t u=0; u<i; ++u) {
            T* t = pa.allocate(1);
            ptrs.push_front(t);
        }

        // ensure pool can be emptied
        EXPECT_EQ(0, pa.available());

        while(ptrs.size()) {
            pa.deallocate(ptrs.front(), 1);
            ptrs.pop_front();
        }
    }
}

}

TEST(pool_allocator, allocate_deallocate) {
    allocate_deallocate_T<int>();
    allocate_deallocate_T<unsigned int>();
    allocate_deallocate_T<size_t>();
    allocate_deallocate_T<float>();
    allocate_deallocate_T<double>();
    allocate_deallocate_T<char>();
    allocate_deallocate_T<std::string>();
    allocate_deallocate_T<test::CustomObject>();
}

TEST(pool_allocator, introspect_pool) {
    introspect_pool_T<int>();
    introspect_pool_T<unsigned int>();
    introspect_pool_T<size_t>();
    introspect_pool_T<float>();
    introspect_pool_T<double>();
    introspect_pool_T<char>();
    introspect_pool_T<std::string>();
    introspect_pool_T<test::CustomObject>();
}
