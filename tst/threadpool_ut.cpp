#include "threadpool.hpp"

#include <gtest/gtest.h> 
#include "test_helpers.hpp"

namespace test {
namespace threadpool {

inline hce::co<void> co_void() { co_return; }

template <typename T>
hce::co<void> co_push_T(test::queue<T>& q, T t) {
    q.push(t);
    co_return;
}

template <typename T>
inline hce::co<T> co_return_T(T t) {
    co_return t;
}

template <typename T>
hce::co<T> co_push_T_return_T(test::queue<T>& q, T t) {
    q.push(t);
    co_return t;
}

template <typename T>
hce::co<T> co_push_T_yield_void_and_return_T(test::queue<T>& q, T t) {
    q.push(t);
    co_await hce::yield<void>();
    co_return t;
}

template <typename T>
hce::co<T> co_push_T_yield_T_and_return_T(test::queue<T>& q, T t) {
    q.push(t);
    co_return co_await hce::yield<T>(t);
}

template <typename T>
size_t schedule_T(std::function<hce::co<void>(test::queue<T>& q, T t)> Coroutine) {
    std::string T_name = hce::type::name<T>();

    HCE_INFO_LOG("schedule_T<%s>",T_name.c_str());

    size_t success_count = 0;

    // schedule individually
    {
        test::queue<T> q;
        HCE_INFO_LOG("schedule_T<%s> started scheduler",T_name.c_str());

        struct launcher {
            static hce::co<void> op(test::queue<T>* q, std::function<hce::co<void>(test::queue<T>& q, T t)>* Coroutine) {
                auto awt1 = hce::schedule((*Coroutine)(*q,test::init<T>(3)));
                auto awt2 = hce::schedule((*Coroutine)(*q,test::init<T>(2)));
                auto awt3 = hce::schedule((*Coroutine)(*q,test::init<T>(1)));
                co_await awt1;
                co_await awt2;
                co_await awt3;
                co_return;
            }
        };

        auto awt = hce::threadpool::schedule(launcher::op(&q,&Coroutine));
        
        HCE_INFO_LOG("schedule_T<%s> launched coroutines",T_name.c_str());

        try {
            EXPECT_EQ((T)test::init<T>(3), q.pop());
            EXPECT_EQ((T)test::init<T>(2), q.pop());
            EXPECT_EQ((T)test::init<T>(1), q.pop());

            ++success_count;
            HCE_INFO_LOG("schedule_T<%s> received values",T_name.c_str());
        } catch(const std::exception& e) {
            LOG_F(ERROR, e.what());
        }

        HCE_INFO_LOG("schedule_T<%s> end of scope",T_name.c_str());
    }

    HCE_INFO_LOG("schedule_T<%s> done",T_name.c_str());

    return success_count;
}

}
}

TEST(threadpool, schedule) {
    // the count of schedule subtests we expect to complete without throwing 
    // exceptions
    const size_t expected = 1;
    EXPECT_EQ(expected, test::threadpool::schedule_T<int>(test::threadpool::co_push_T<int>));
    EXPECT_EQ(expected, test::threadpool::schedule_T<unsigned int>(test::threadpool::co_push_T<unsigned int>));
    EXPECT_EQ(expected, test::threadpool::schedule_T<size_t>(test::threadpool::co_push_T<size_t>));
    EXPECT_EQ(expected, test::threadpool::schedule_T<float>(test::threadpool::co_push_T<float>));
    EXPECT_EQ(expected, test::threadpool::schedule_T<double>(test::threadpool::co_push_T<double>));
    EXPECT_EQ(expected, test::threadpool::schedule_T<char>(test::threadpool::co_push_T<char>));
    EXPECT_EQ(expected, test::threadpool::schedule_T<void*>(test::threadpool::co_push_T<void*>));
    EXPECT_EQ(expected, test::threadpool::schedule_T<std::string>(test::threadpool::co_push_T<std::string>));
    EXPECT_EQ(expected, test::threadpool::schedule_T<test::CustomObject>(test::threadpool::co_push_T<test::CustomObject>));
}

/*
test::threadpool::co_push_T_yield_void_and_return_T
test::threadpool::co_push_T_yield_T_and_return_T
*/
TEST(threadpool, schedule_yield) {
    // the count of schedule subtests we expect to complete without throwing 
    // exceptions
    const size_t expected = 1;

    // yield then return
    {
        EXPECT_EQ(expected, test::threadpool::schedule_T<int>(test::threadpool::co_push_T_yield_void_and_return_T<int>));
        EXPECT_EQ(expected, test::threadpool::schedule_T<unsigned int>(test::threadpool::co_push_T_yield_void_and_return_T<unsigned int>));
        EXPECT_EQ(expected, test::threadpool::schedule_T<size_t>(test::threadpool::co_push_T_yield_void_and_return_T<size_t>));
        EXPECT_EQ(expected, test::threadpool::schedule_T<float>(test::threadpool::co_push_T_yield_void_and_return_T<float>));
        EXPECT_EQ(expected, test::threadpool::schedule_T<double>(test::threadpool::co_push_T_yield_void_and_return_T<double>));
        EXPECT_EQ(expected, test::threadpool::schedule_T<char>(test::threadpool::co_push_T_yield_void_and_return_T<char>));
        EXPECT_EQ(expected, test::threadpool::schedule_T<void*>(test::threadpool::co_push_T_yield_void_and_return_T<void*>));
        EXPECT_EQ(expected, test::threadpool::schedule_T<std::string>(test::threadpool::co_push_T_yield_void_and_return_T<std::string>));
        EXPECT_EQ(expected, test::threadpool::schedule_T<test::CustomObject>(test::threadpool::co_push_T_yield_void_and_return_T<test::CustomObject>));
    }

    // yield *into* a return
    {
        EXPECT_EQ(expected, test::threadpool::schedule_T<int>(test::threadpool::co_push_T_yield_T_and_return_T<int>));
        EXPECT_EQ(expected, test::threadpool::schedule_T<unsigned int>(test::threadpool::co_push_T_yield_T_and_return_T<unsigned int>));
        EXPECT_EQ(expected, test::threadpool::schedule_T<size_t>(test::threadpool::co_push_T_yield_T_and_return_T<size_t>));
        EXPECT_EQ(expected, test::threadpool::schedule_T<float>(test::threadpool::co_push_T_yield_T_and_return_T<float>));
        EXPECT_EQ(expected, test::threadpool::schedule_T<double>(test::threadpool::co_push_T_yield_T_and_return_T<double>));
        EXPECT_EQ(expected, test::threadpool::schedule_T<char>(test::threadpool::co_push_T_yield_T_and_return_T<char>));
        EXPECT_EQ(expected, test::threadpool::schedule_T<void*>(test::threadpool::co_push_T_yield_T_and_return_T<void*>));
        EXPECT_EQ(expected, test::threadpool::schedule_T<std::string>(test::threadpool::co_push_T_yield_T_and_return_T<std::string>));
        EXPECT_EQ(expected, test::threadpool::schedule_T<test::CustomObject>(test::threadpool::co_push_T_yield_T_and_return_T<test::CustomObject>));
    }
}

namespace test {
namespace threadpool {

template <typename T>
size_t join_schedule_T() {
    std::string T_name = []() -> std::string {
        std::stringstream ss;
        ss << typeid(T).name();
        return ss.str();
    }();

    HCE_INFO_LOG("join_schedule_T<%s>",T_name.c_str());
    size_t success_count = 0;

    // schedule individually
    {
        std::deque<hce::awt<T>> schedules;

        schedules.push_back(hce::threadpool::schedule(
            co_return_T<T>(test::init<T>(3))));
        schedules.push_back(hce::threadpool::schedule(
            co_return_T<T>(test::init<T>(2))));
        schedules.push_back(hce::threadpool::schedule(
            co_return_T<T>(test::init<T>(1))));

        try {
            T result = schedules.front();
            schedules.pop_front();
            EXPECT_EQ((T)test::init<T>(3), result);
            result = schedules.front();
            schedules.pop_front();
            EXPECT_EQ((T)test::init<T>(2), result);
            result = schedules.front();
            schedules.pop_front();
            EXPECT_EQ((T)test::init<T>(1), result);

            ++success_count;
        } catch(const std::exception& e) {
            LOG_F(ERROR, e.what());
        }
    } 

    // schedule individually in reverse order
    {
        std::deque<hce::awt<T>> schedules;

        schedules.push_back(hce::threadpool::schedule(co_return_T<T>(test::init<T>(3))));
        schedules.push_back(hce::threadpool::schedule(co_return_T<T>(test::init<T>(2))));
        schedules.push_back(hce::threadpool::schedule(co_return_T<T>(test::init<T>(1))));

        try {
            T result = schedules.back();
            schedules.pop_back();
            EXPECT_EQ((T)test::init<T>(1), result);
            result = schedules.back();
            schedules.pop_back();
            EXPECT_EQ((T)test::init<T>(2), result);
            result = schedules.back();
            schedules.pop_back();
            EXPECT_EQ((T)test::init<T>(3), result);

            ++success_count;
        } catch(const std::exception& e) {
            LOG_F(ERROR, e.what());
        }
    }

    // schedule void
    {
        std::deque<hce::awt<void>> schedules;

        schedules.push_back(hce::threadpool::schedule(co_void()));
        schedules.push_back(hce::threadpool::schedule(co_void()));
        schedules.push_back(hce::threadpool::schedule(co_void()));

        try {
            schedules.pop_front();
            schedules.pop_front();
            schedules.pop_front();

            ++success_count;
        } catch(const std::exception& e) {
            LOG_F(ERROR, e.what());
        }
    }

    return success_count;
}

}
}

TEST(threadpool, join_schedule) {
    const size_t expected = 3;
    EXPECT_EQ(expected, test::threadpool::join_schedule_T<int>());
    EXPECT_EQ(expected, test::threadpool::join_schedule_T<unsigned int>());
    EXPECT_EQ(expected, test::threadpool::join_schedule_T<size_t>());
    EXPECT_EQ(expected, test::threadpool::join_schedule_T<float>());
    EXPECT_EQ(expected, test::threadpool::join_schedule_T<double>());
    EXPECT_EQ(expected, test::threadpool::join_schedule_T<char>());
    EXPECT_EQ(expected, test::threadpool::join_schedule_T<void*>());
    EXPECT_EQ(expected, test::threadpool::join_schedule_T<std::string>());
    EXPECT_EQ(expected, test::threadpool::join_schedule_T<test::CustomObject>());
}

namespace test {
namespace threadpool {

hce::co<void> cache_info_check_co() {
    HCE_INFO_FUNCTION_ENTER("cache_info_check_co", hce::config::memory::cache::info::thread::type::scheduler);
    auto& info = hce::config::memory::cache::info::get();
    auto type = hce::config::memory::cache::info::thread::get_type();

    EXPECT_TRUE(hce::config::memory::cache::info::thread::type::system != type);
    EXPECT_EQ(HCETHREADLOCALMEMORYBUCKETCOUNT, info.count());

    const size_t byte_limit = 
        type == hce::config::memory::cache::info::thread::type::global
        ? HCETHREADLOCALMEMORYBUCKETBYTELIMIT * 2
        : HCETHREADLOCALMEMORYBUCKETBYTELIMIT;

    for(size_t i=0; i<info.count(); ++i) { 
        auto& bucket = info.at(i);
        const size_t block_size = 1 << i;

        EXPECT_EQ(1 << i, bucket.block);

        if(block_size > byte_limit) {
            EXPECT_EQ(1, bucket.limit);
        } else {
            size_t limit_calculation = byte_limit / block_size;
            EXPECT_EQ(limit_calculation, bucket.limit);
        }
    }

    co_return;
}

hce::co<void> cache_allocate_deallocate_co() {
    auto& cache = hce::memory::cache::get();

    EXPECT_EQ(HCETHREADLOCALMEMORYBUCKETCOUNT, cache.count());

    // ensure caching works for each bucket
    for(size_t i=0; i < HCETHREADLOCALMEMORYBUCKETCOUNT; ++i) {
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

    co_return;
}

}
}

TEST(threadpool, cache_info) {
    auto& tp_schs = hce::threadpool::get().schedulers();

    EXPECT_TRUE(tp_schs.size() > 0);

    std::vector<hce::awt<void>> awts;

    for(size_t i=0; i<tp_schs.size(); ++i) {
        awts.push_back(tp_schs[i]->schedule(test::threadpool::cache_info_check_co()));
    }
}

TEST(threadpool, cache_allocate_deallocate) {
    auto& tp_schs = hce::threadpool::get().schedulers();

    EXPECT_TRUE(tp_schs.size() > 0);

    std::vector<hce::awt<void>> awts;

    for(size_t i=0; i<tp_schs.size(); ++i) {
        awts.push_back(tp_schs[i]->schedule(test::threadpool::cache_allocate_deallocate_co()));
    }
}
