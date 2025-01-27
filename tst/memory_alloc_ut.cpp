//SPDX-License-Identifier: Apache-2.0
//Author: Blayne Dennis 
#include <string>

#include "memory.hpp"

#include <gtest/gtest.h>
#include "test_helpers.hpp"
#include "test_memory_helpers.hpp"

namespace test {

template <typename T> 
void allocate_deallocate_T(){
    for(size_t i=0; i<100; ++i) {
        // can allocate valid memory
        T* t = hce::allocate<T>();
        EXPECT_NE(nullptr, t);

        // can construct without segfault
        new(t) T((T)(test::init<T>(i)));
        EXPECT_EQ((T)test::init<T>(i),*t);

        // can destruct without segfault
        t->~T();
        hce::deallocate<T>(t);
    }
}

}

TEST(memory, system_cache_info) {
    hce::lifecycle::config c;
    test::memory::cache_info_check("system", c.mem.system);
}

TEST(memory, system_cache_allocate_deallocate) {
    test::memory::cache_allocate_deallocate();
}

TEST(memory, allocate_deallocate) {
    test::allocate_deallocate_T<int>();
    test::allocate_deallocate_T<unsigned int>();
    test::allocate_deallocate_T<size_t>();
    test::allocate_deallocate_T<float>();
    test::allocate_deallocate_T<double>();
    test::allocate_deallocate_T<char>();
    test::allocate_deallocate_T<std::string>();
    test::allocate_deallocate_T<test::CustomObject>();
}
