//SPDX-License-Identifier: Apache-2.0
//Author: Blayne Dennis 
#ifndef __HCE_COROUTINE_ENGINE_TEST_MEMORY_HELPERS__
#define __HCE_COROUTINE_ENGINE_TEST_MEMORY_HELPERS__
#include <vector>
#include <string>

#include "logging.hpp"
#include "memory.hpp"
#include "coroutine.hpp"
#include "lifecycle.hpp"

namespace test {
namespace memory {

inline void cache_info_check(
        const char* expected_name, 
        const hce::config::memory::cache::info* expected_impl)
{
    HCE_INFO_FUNCTION_ENTER("test::memory::cache_info_check_co", expected_impl);
    auto& info = hce::config::memory::cache::info::get();
    auto* actual_impl = &(info);

    std::string expected_name_str(expected_name);
    std::string actual_name_str(info.name());
    EXPECT_EQ(expected_name_str, actual_name_str);
    EXPECT_EQ(expected_impl, actual_impl);
    EXPECT_EQ(HCEMEMORYCACHEBUCKETCOUNT, info.count());

    for(size_t i=0; i<info.count(); ++i) { 
        auto& bucket = info.at(i);
        EXPECT_LT(0,bucket.block);
        EXPECT_LT(0,bucket.limit);
        auto byte_limit = bucket.block * bucket.limit;
        const size_t block_size = 1 << i;

        EXPECT_EQ(1 << i, bucket.block);

        if(block_size > byte_limit) {
            EXPECT_EQ(1, bucket.limit);
        } else {
            size_t limit_calculation = byte_limit / block_size;
            EXPECT_EQ(limit_calculation, bucket.limit);
        }
    }
}

inline hce::co<void> cache_info_check_co(
        const char* expected_name,
        const hce::config::memory::cache::info* expected_impl)
{
    cache_info_check(expected_name, expected_impl);
    co_return;
}

inline void cache_allocate_deallocate() {
    auto& cache = hce::memory::cache::get();

    EXPECT_EQ(HCEMEMORYCACHEBUCKETCOUNT, cache.count());

    // ensure caching works for each bucket
    for(size_t i=0; i < HCEMEMORYCACHEBUCKETCOUNT; ++i) {
        const size_t cur_bucket_block_size = 1 << i;
        const size_t prev_bucket_block_size = i ? 1 << (i-1) : 0;
            
        // ensure we select the right bucket for each bucket size
        EXPECT_EQ(i, cache.index(cur_bucket_block_size));

        // ensure caching works for each potential block size in every bucket
        for(size_t block_size = prev_bucket_block_size+1; 
            block_size <= cur_bucket_block_size; 
            ++block_size) 
        {
            // ensure we are hitting the right bucket each time
            EXPECT_EQ(i, cache.index(block_size));

            // ensure cache is empty
            cache.clear();

            for(size_t block_size = prev_bucket_block_size + 1; 
                block_size <= cur_bucket_block_size; 
                ++block_size) 
            {
                EXPECT_EQ(0, cache.available(block_size));
            }

            std::vector<void*> allocations;

            // fill cache
            for(size_t i=0; i < cache.limit(block_size); ++i) {
                void* p = hce::memory::allocate(block_size);
                allocations.push_back(p);
            }

            while(cache.available(block_size) < cache.limit(block_size)) {
                size_t expected_available = cache.available(block_size);
                ASSERT_TRUE(!(allocations.empty()));
                void* p = allocations.back();
                allocations.pop_back();
                hce::memory::deallocate(p);

                if(expected_available == cache.limit(block_size)) {
                    EXPECT_EQ(expected_available, cache.available(block_size));
                } else {
                    ++expected_available;
                    EXPECT_EQ(expected_available, cache.available(block_size));
                }
            }
                
            const size_t expected_available_past_max = cache.limit(block_size);

            // deallocate past the cache limit and ensure memory is freed instead
            for(size_t u=0; u<cache.limit(block_size); ++u) {
                hce::memory::deallocate(hce::memory::allocate(block_size));
                EXPECT_EQ(expected_available_past_max, cache.available(block_size));
            }

            // empty cache
            while(cache.available(block_size)) {
                const size_t available = cache.available(block_size);
                const size_t expected_available_post_alloc = available ? available - 1 : 0;

                void* mem = hce::memory::allocate(block_size);
                EXPECT_NE(nullptr, mem);
                allocations.push_back(mem);

                EXPECT_EQ(expected_available_post_alloc, cache.available(block_size));
            }

            // refill cache from empty
            while(cache.available(block_size) < cache.limit(block_size)) {
                const size_t available = hce::memory::cache::get().available(block_size);
                const size_t expected_available_post_dealloc = available + 1;
                hce::memory::deallocate(allocations.back());
                allocations.pop_back();

                EXPECT_EQ(expected_available_post_dealloc, hce::memory::cache::get().available(block_size));
            }

            // ensure we've done our math right so no hanging allocations
            ASSERT_EQ(0, allocations.size());
        }
    }
}

inline hce::co<void> cache_allocate_deallocate_co() {
    cache_allocate_deallocate();
    co_return;
}

}
}

#endif
