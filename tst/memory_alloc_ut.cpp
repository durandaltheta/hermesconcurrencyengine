//SPDX-License-Identifier: Apache-2.0
//Author: Blayne Dennis 
#include <string>

#include "hce/memory.hpp"
#include "hce/alloc.hpp"

#include <gtest/gtest.h>
#include "test_helpers.hpp"

namespace {

template <typename T> 
void mem_allocate_deallocate_T(){
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

TEST(memory, allocate_deallocate) {
    mem_allocate_deallocate_T<int>();
    mem_allocate_deallocate_T<unsigned int>();
    mem_allocate_deallocate_T<size_t>();
    mem_allocate_deallocate_T<float>();
    mem_allocate_deallocate_T<double>();
    mem_allocate_deallocate_T<char>();
    mem_allocate_deallocate_T<std::string>();
    mem_allocate_deallocate_T<test::CustomObject>();
}
