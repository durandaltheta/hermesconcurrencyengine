//SPDX-License-Identifier: Apache-2.0
//Author: Blayne Dennis 
#include <string>

#include "memory.hpp"

#include <gtest/gtest.h>
#include "test_helpers.hpp"

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
    auto& info = hce::config::memory::cache::info::get();
    auto type = hce::config::memory::cache::info::thread::get_type();

    EXPECT_EQ(hce::config::memory::cache::info::thread::type::system, type);
    EXPECT_EQ(HCETHREADLOCALMEMORYBUCKETCOUNT, info.count());

    const size_t byte_limit = sizeof(void*) * 64;

    for(size_t i=0; i<info.count(); ++i) { 
        auto& bucket = info.at(i);
        const size_t block_size = 1 << i;

        EXPECT_EQ(1 << i, bucket.block);

        if(block_size > byte_limit) {
            EXPECT_EQ(1, bucket.limit);
        } else {
            const size_t limit_calculation = byte_limit / block_size;
            EXPECT_EQ(limit_calculation, bucket.limit);
        }
    }
}

TEST(memory, system_cache_allocate_deallocate) {
    auto& cache = hce::memory::cache::get();

    EXPECT_EQ(HCETHREADLOCALMEMORYBUCKETCOUNT, cache.count());

    // ensure caching works for each bucket
    for(size_t i=0; i < HCETHREADLOCALMEMORYBUCKETCOUNT; ++i) {
        const size_t cur_bucket_block_size = 1 << i;
        const size_t prev_bucket_block_size = i ? 1 << (i-1) : 0;
            
        // ensure we select the right bucket for each bucket size
        EXPECT_EQ(i, cache.index(cur_bucket_block_size));

        /* 
         Ensure caching works for each potential block size in every bucket.
         Calculation begins at +1 the previous bucket block size to ensure our 
         range begins at a value larger than the previous bucket can handle.
         */
        for(size_t block_size = prev_bucket_block_size + 1; 
            block_size <= cur_bucket_block_size; 
            ++block_size) 
        {
            // ensure we are hitting the right bucket each time
            EXPECT_EQ(i, cache.index(block_size));

            // fill cache
            while(cache.available(block_size) < cache.limit(block_size)) {
                hce::memory::deallocate(std::malloc(sizeof(block_size)), block_size);
            }

            const size_t expected_available_past_max = cache.limit(block_size);

            // deallocate past the cache limit and ensure memory is freed instead
            for(size_t u=0; u<cache.limit(block_size); ++u) {
                hce::memory::deallocate(std::malloc(sizeof(block_size)), block_size);
                EXPECT_EQ(expected_available_past_max, cache.available(block_size));
            }

            std::vector<void*> allocations;

            // empty cache
            while(cache.available(block_size)) {
                const size_t available = hce::memory::cache::get().available(block_size);
                const size_t expected_available_post_alloc = available ? available - 1 : 0;

                void* mem = hce::memory::allocate(block_size);
                EXPECT_NE(nullptr, mem);
                allocations.push_back(mem);

                EXPECT_EQ(expected_available_post_alloc, hce::memory::cache::get().available(block_size));
            }

            // refill cache from empty
            while(cache.available(block_size) < cache.limit(block_size)) {
                const size_t available = hce::memory::cache::get().available(block_size);
                const size_t expected_available_post_dealloc = available + 1;
                hce::memory::deallocate(allocations.back(), block_size);
                allocations.pop_back();

                EXPECT_EQ(expected_available_post_dealloc, hce::memory::cache::get().available(block_size));
            }
           
            // leave cache empty for next time
            while(cache.available(block_size)) {
                std::free(hce::memory::allocate(block_size));
            }
        }
    }
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
