//SPDX-License-Identifier: Apache-2.0
//Author: Blayne Dennis 
#include <deque>
#include <string>
#include <thread>
#include <chrono>
#include <vector>

#include "loguru.hpp"
#include "atomic.hpp"
#include "scheduler.hpp"

#include <gtest/gtest.h> 
#include "test_helpers.hpp"

namespace test {
namespace scheduler {

template <typename T>
T block_done_immediately_T(T t, bool& ids_identical, std::thread::id parent_id) {
    ids_identical = parent_id == std::this_thread::get_id();
    return std::move(t);
}

void block_done_immediately_void(bool& ids_identical, std::thread::id parent_id) {
    ids_identical = parent_id == std::this_thread::get_id();
    return;
}

template <typename T>
T block_done_immediately_stacked_outer_T(T t, bool& ids_identical, std::thread::id parent_id) {
    auto thd_id = std::this_thread::get_id();
    ids_identical = parent_id == thd_id;
    bool sub_ids_identical = false;
    T result = hce::scheduler::get().block(block_done_immediately_T<T>,std::move(t), std::ref(sub_ids_identical), thd_id);
    EXPECT_TRUE(sub_ids_identical);
    return result;
}

void block_done_immediately_stacked_outer_void(bool& ids_identical, std::thread::id parent_id) {
    auto thd_id = std::this_thread::get_id();
    ids_identical = parent_id == thd_id;
    bool sub_ids_identical = false;
    hce::scheduler::get().block(block_done_immediately_void, std::ref(sub_ids_identical), thd_id);
    EXPECT_TRUE(sub_ids_identical);
    return;
}

template <typename T>
hce::co<T> co_block_done_immediately_T(T t, bool& ids_identical, std::thread::id parent_id) {
    HCE_INFO_FUNCTION_BODY("co_block_done_immediately_T",hce::coroutine::local());
    auto thd_id = std::this_thread::get_id();
    ids_identical = parent_id == thd_id;
    bool sub_ids_identical = true;
    T result = co_await hce::scheduler::get().block(block_done_immediately_T<T>,std::move(t),std::ref(sub_ids_identical), parent_id);
    EXPECT_FALSE(sub_ids_identical);
    co_return std::move(result);
}

hce::co<void> co_block_done_immediately_void(bool& ids_identical, std::thread::id parent_id) {
    HCE_INFO_FUNCTION_BODY("co_block_done_immediately_void",hce::coroutine::local());
    auto thd_id = std::this_thread::get_id();
    ids_identical = parent_id == thd_id;
    bool sub_ids_identical = true;
    co_await hce::scheduler::get().block(block_done_immediately_void,std::ref(sub_ids_identical), parent_id);
    EXPECT_FALSE(sub_ids_identical);
    co_return;
}

template <typename T>
hce::co<T> co_block_done_immediately_stacked_outer_T(T t, bool& ids_identical, std::thread::id parent_id) {
    HCE_INFO_FUNCTION_BODY("co_block_done_immediately_stacked_outer_T",hce::coroutine::local());
    auto thd_id = std::this_thread::get_id();
    ids_identical = parent_id == thd_id;
    bool sub_ids_identical = true;
    T result = co_await hce::scheduler::get().block(block_done_immediately_stacked_outer_T<T>,std::move(t),std::ref(sub_ids_identical), parent_id);
    EXPECT_FALSE(sub_ids_identical);
    co_return std::move(result);
}

hce::co<void> co_block_done_immediately_stacked_outer_void(bool& ids_identical, std::thread::id parent_id) {
    HCE_INFO_FUNCTION_BODY("co_block_done_immediately_stacked_outer_void",hce::coroutine::local());
    auto thd_id = std::this_thread::get_id();
    ids_identical = parent_id == thd_id;
    bool sub_ids_identical = true;
    co_await hce::scheduler::get().block(block_done_immediately_stacked_outer_void,std::ref(sub_ids_identical), parent_id);
    EXPECT_FALSE(sub_ids_identical);
    co_return;
}

template <typename T>
T block_for_queue_T(test::queue<T>& q, bool& ids_identical, std::thread::id parent_id) {
    ids_identical = parent_id == std::this_thread::get_id();
    return q.pop();
}

void block_for_queue_void(test::queue<void>& q, bool& ids_identical, std::thread::id parent_id) {
    ids_identical = parent_id == std::this_thread::get_id();
    q.pop();
    return;
}

template <typename T>
T block_for_queue_stacked_outer_T(test::queue<T>& q, bool& ids_identical, std::thread::id parent_id) {
    auto thd_id = std::this_thread::get_id();
    ids_identical = parent_id == thd_id;
    bool sub_ids_identical = false;
    T result = hce::scheduler::get().block(block_for_queue_T<T>,std::ref(q), std::ref(sub_ids_identical), thd_id);
    EXPECT_TRUE(sub_ids_identical);
    return result;
}

void block_for_queue_stacked_outer_void(test::queue<void>& q, bool& ids_identical, std::thread::id parent_id) {
    auto thd_id = std::this_thread::get_id();
    ids_identical = parent_id == thd_id;
    bool sub_ids_identical = false;
    hce::scheduler::get().block(block_for_queue_void,std::ref(q), std::ref(sub_ids_identical), thd_id);
    EXPECT_TRUE(sub_ids_identical);
    return;
}

template <typename T>
hce::co<T> co_block_for_queue_T(test::queue<T>& q, bool& ids_identical, std::thread::id parent_id) {
    HCE_INFO_FUNCTION_BODY("co_block_for_queue_T","T: ",typeid(T).name(),", coroutine: ",hce::coroutine::local());
    auto thd_id = std::this_thread::get_id();
    ids_identical = parent_id == thd_id;
    bool sub_ids_identical = true;
    T result = co_await hce::scheduler::get().block(block_for_queue_T<T>,std::ref(q),std::ref(sub_ids_identical), parent_id);
    EXPECT_FALSE(sub_ids_identical);
    co_return std::move(result);
}

hce::co<void> co_block_for_queue_void(test::queue<void>& q, bool& ids_identical, std::thread::id parent_id) {
    HCE_INFO_FUNCTION_BODY("co_block_for_queue_void","coroutine: ",hce::coroutine::local());
    auto thd_id = std::this_thread::get_id();
    ids_identical = parent_id == thd_id;
    bool sub_ids_identical = true;
    co_await hce::scheduler::get().block(block_for_queue_void,std::ref(q),std::ref(sub_ids_identical), parent_id);
    EXPECT_FALSE(sub_ids_identical);
    co_return;
}

template <typename T>
hce::co<T> co_block_for_queue_stacked_outer_T(test::queue<T>& q, bool& ids_identical, std::thread::id parent_id) {
    HCE_INFO_FUNCTION_BODY("co_block_for_queue_stacked_outer_T","T: ",typeid(T).name(),", coroutine: ",hce::coroutine::local());
    auto thd_id = std::this_thread::get_id();
    ids_identical = parent_id == thd_id;
    bool sub_ids_identical = true;
    T result = co_await hce::scheduler::get().block(block_for_queue_stacked_outer_T<T>,std::ref(q),std::ref(sub_ids_identical), parent_id);
    EXPECT_FALSE(sub_ids_identical);
    co_return std::move(result);
}

hce::co<void> co_block_for_queue_stacked_outer_void(test::queue<void>& q, bool& ids_identical, std::thread::id parent_id) {
    HCE_INFO_FUNCTION_BODY("co_block_for_queue_stacked_outer_void",hce::coroutine::local());
    HCE_INFO_FUNCTION_BODY("co_block_for_queue_stacked_outer_void","coroutine: ",hce::coroutine::local());
    auto thd_id = std::this_thread::get_id();
    ids_identical = parent_id == thd_id;
    bool sub_ids_identical = true;
    co_await hce::scheduler::get().block(block_for_queue_stacked_outer_void,std::ref(q),std::ref(sub_ids_identical), parent_id);
    EXPECT_FALSE(sub_ids_identical);
    co_return;
}

template <typename T>
size_t block_T() {
    size_t success_count = 0;

    {
        HCE_INFO_LOG("thread block done immediately+");
        auto schedule_blocking = [&](T t) {
            auto thd_id = std::this_thread::get_id();
            bool ids_identical = false;
            bool ids_identical2 = false;
            bool ids_identical3 = false;
            EXPECT_EQ(0, hce::scheduler::get().block_worker_count());
            EXPECT_EQ(t, (T)hce::scheduler::get().block(block_done_immediately_T<T>,t,std::ref(ids_identical), thd_id));
            EXPECT_TRUE(ids_identical);
            EXPECT_EQ(t, (T)hce::scheduler::get().block(block_done_immediately_T<T>,t,std::ref(ids_identical2), thd_id));
            EXPECT_TRUE(ids_identical2);
            EXPECT_EQ(t, (T)hce::block(block_done_immediately_T<T>,t,std::ref(ids_identical3), thd_id));
            EXPECT_TRUE(ids_identical3);
            EXPECT_EQ(0, hce::scheduler::get().block_worker_count());
        };

        try {
            schedule_blocking((T)test::init<T>(3));
            schedule_blocking((T)test::init<T>(2));
            schedule_blocking((T)test::init<T>(1));

            ++success_count;
        } catch(const std::exception& e) {
            LOG_F(ERROR, e.what());
        }
        HCE_INFO_LOG("thread block done immediately-");
    }

    {
        HCE_INFO_LOG("thread block for queue+");
        auto schedule_blocking = [&](T t) {
            test::queue<T> q;
            auto thd_id = std::this_thread::get_id();
            bool ids_identical = false;
            bool ids_identical2 = false;
            bool ids_identical3 = false;

            auto launch_sender_thd = [&]{
                std::thread([&](T t){
                    EXPECT_EQ(0, hce::scheduler::get().block_worker_count());
                    q.push(std::move(t));
                },t).detach();
            };

            EXPECT_EQ(0, hce::scheduler::get().block_worker_count());
            launch_sender_thd();
            launch_sender_thd();
            launch_sender_thd();
            EXPECT_EQ(t, (T)hce::scheduler::get().block(block_for_queue_T<T>,std::ref(q),std::ref(ids_identical), thd_id));
            EXPECT_TRUE(ids_identical);
            EXPECT_EQ(t, (T)hce::scheduler::get().block(block_for_queue_T<T>,std::ref(q),std::ref(ids_identical2), thd_id));
            EXPECT_TRUE(ids_identical2);
            EXPECT_EQ(t, (T)hce::block(block_for_queue_T<T>,std::ref(q),std::ref(ids_identical3), thd_id));
            EXPECT_TRUE(ids_identical3);
            EXPECT_EQ(0, hce::scheduler::get().block_worker_count());
        };

        try {
            schedule_blocking((T)test::init<T>(3));
            schedule_blocking((T)test::init<T>(2));
            schedule_blocking((T)test::init<T>(1));

            ++success_count;
        } catch(const std::exception& e) {
            LOG_F(ERROR, e.what());
        }
        HCE_INFO_LOG("thread block for queue-");
    }

    //
    // thread stacked block done immediately 

    // When block() calls are stacked (block() calls block()), the inner block()
    // call should execute immediately on the current thread, leaving the 
    // 'block_worker_count()' count the same as only calling block() once. 
    {
        HCE_INFO_LOG("thread stacked block done immediately+");
        auto schedule_blocking = [&](T t) {
            auto thd_id = std::this_thread::get_id();
            bool ids_identical = false;
            bool ids_identical2 = false;
            bool ids_identical3 = false;
            EXPECT_EQ(0, hce::scheduler::get().block_worker_count());
            EXPECT_EQ(t, (T)hce::scheduler::get().block(block_done_immediately_stacked_outer_T<T>,t,std::ref(ids_identical), thd_id));
            EXPECT_TRUE(ids_identical);
            EXPECT_EQ(t, (T)hce::scheduler::get().block(block_done_immediately_stacked_outer_T<T>,t,std::ref(ids_identical2), thd_id));
            EXPECT_TRUE(ids_identical2);
            EXPECT_EQ(t, (T)hce::block(block_done_immediately_stacked_outer_T<T>,t,std::ref(ids_identical3), thd_id));
            EXPECT_TRUE(ids_identical3);
            EXPECT_EQ(0, hce::scheduler::get().block_worker_count());
        };

        try {
            schedule_blocking((T)test::init<T>(3));
            schedule_blocking((T)test::init<T>(2));
            schedule_blocking((T)test::init<T>(1));

            ++success_count;
        } catch(const std::exception& e) {
            LOG_F(ERROR, e.what());
        }
        HCE_INFO_LOG("thread stacked block done immediately-");
    }

    {
        HCE_INFO_LOG("thread stacked block+");
        auto schedule_blocking = [&](T t) {
            test::queue<T> q;
            auto thd_id = std::this_thread::get_id();
            bool ids_identical = false;
            bool ids_identical2 = false;
            bool ids_identical3 = false;

            auto launch_sender_thd = [&]{
                std::thread([&](T t){
                    EXPECT_EQ(0, hce::scheduler::get().block_worker_count());
                    q.push(std::move(t));
                },t).detach();
            };

            EXPECT_EQ(0, hce::scheduler::get().block_worker_count());
            launch_sender_thd();
            launch_sender_thd();
            launch_sender_thd();
            EXPECT_EQ(t, (T)hce::scheduler::get().block(block_for_queue_stacked_outer_T<T>,std::ref(q),std::ref(ids_identical), thd_id));
            EXPECT_TRUE(ids_identical);
            EXPECT_EQ(t, (T)hce::scheduler::get().block(block_for_queue_stacked_outer_T<T>,std::ref(q),std::ref(ids_identical2), thd_id));
            EXPECT_TRUE(ids_identical2);
            EXPECT_EQ(t, (T)hce::block(block_for_queue_stacked_outer_T<T>,std::ref(q),std::ref(ids_identical3), thd_id));
            EXPECT_TRUE(ids_identical3);
            EXPECT_EQ(0, hce::scheduler::get().block_worker_count());
        };

        try {
            schedule_blocking((T)test::init<T>(3));
            schedule_blocking((T)test::init<T>(2));
            schedule_blocking((T)test::init<T>(1));

            ++success_count;
        } catch(const std::exception& e) {
            LOG_F(ERROR, e.what());
        }
        HCE_INFO_LOG("thread stacked block-");
    }

    {
        HCE_INFO_LOG("coroutine block done immediately+");
        test::queue<T> q;
        auto lf = hce::scheduler::make();
        std::shared_ptr<hce::scheduler> sch = lf->scheduler();

        EXPECT_EQ(0, sch->block_worker_pool_limit());

        auto schedule_blocking_co = [&](T t) {
            HCE_INFO_FUNCTION_ENTER("schedule_blocking_co");
            auto thd_id = std::this_thread::get_id();
            bool co_ids_identical = true;
            bool co_ids_identical2 = true;
            bool co_ids_identical3 = true;

            HCE_INFO_LOG("block done immediately 1");
            auto awt = sch->schedule(
                test::scheduler::co_block_done_immediately_T(
                    t,
                    co_ids_identical, 
                    thd_id));

            HCE_INFO_LOG("block done immediately 2");
            auto awt2 = sch->schedule(
                test::scheduler::co_block_done_immediately_T(
                    t,
                    co_ids_identical2, 
                    thd_id));

            HCE_INFO_LOG("block done immediately 3");
            auto awt3 = sch->schedule(
                test::scheduler::co_block_done_immediately_T(
                    t,
                    co_ids_identical3, 
                    thd_id));

            EXPECT_EQ(t, (T)std::move(awt));
            EXPECT_FALSE(co_ids_identical);
            EXPECT_EQ(t, (T)std::move(awt2));
            EXPECT_FALSE(co_ids_identical2);
            EXPECT_EQ(t, (T)std::move(awt3));
            EXPECT_FALSE(co_ids_identical3);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            EXPECT_EQ(0, sch->block_worker_count());
        };

        try {
            schedule_blocking_co((T)test::init<T>(3));
            schedule_blocking_co((T)test::init<T>(2));
            schedule_blocking_co((T)test::init<T>(1));

            ++success_count;
        } catch(const std::exception& e) {
            LOG_F(ERROR, e.what());
        }
        HCE_INFO_LOG("coroutine block done immediately-");
    }

    {
        HCE_INFO_LOG("coroutine block for queue+");
        test::queue<T> q;
        auto lf = hce::scheduler::make();
        std::shared_ptr<hce::scheduler> sch = lf->scheduler();

        auto schedule_blocking_co = [&](T t) {
            auto thd_id = std::this_thread::get_id();
            bool co_ids_identical = true;
            bool co_ids_identical2 = true;
            bool co_ids_identical3 = true;

            HCE_INFO_LOG("block for queue 1");
            auto awt = sch->schedule(
                test::scheduler::co_block_for_queue_T(
                    q,
                    co_ids_identical, 
                    thd_id));

            HCE_INFO_LOG("block for queue 2");
            auto awt2 = sch->schedule(
                test::scheduler::co_block_for_queue_T(
                    q,
                    co_ids_identical2, 
                    thd_id));

            HCE_INFO_LOG("block for queue 3");
            auto awt3 = sch->schedule(
                test::scheduler::co_block_for_queue_T(
                    q,
                    co_ids_identical3, 
                    thd_id));

            std::this_thread::sleep_for(std::chrono::milliseconds(40));
            EXPECT_EQ(3, sch->block_worker_count());
            q.push(t);
            q.push(t);
            q.push(t);
            EXPECT_EQ(t, (T)std::move(awt));
            EXPECT_FALSE(co_ids_identical);
            EXPECT_EQ(t, (T)std::move(awt2));
            EXPECT_FALSE(co_ids_identical2);
            EXPECT_EQ(t, (T)std::move(awt3));
            EXPECT_FALSE(co_ids_identical3);
        };

        try {
            schedule_blocking_co((T)test::init<T>(3));
            schedule_blocking_co((T)test::init<T>(2));
            schedule_blocking_co((T)test::init<T>(1));

            ++success_count;
        } catch(const std::exception& e) {
            LOG_F(ERROR, e.what());
        }
        HCE_INFO_LOG("coroutine block for queue-");
    }

    {
        HCE_INFO_LOG("coroutine stacked block done immediately+");
        test::queue<T> q;
        auto lf = hce::scheduler::make();
        std::shared_ptr<hce::scheduler> sch = lf->scheduler();

        auto schedule_blocking_co = [&](T t) {
            auto thd_id = std::this_thread::get_id();
            bool co_ids_identical = true;
            bool co_ids_identical2 = true;
            bool co_ids_identical3 = true;

            HCE_INFO_LOG("stacked block done immediately join 1");
            auto awt = sch->schedule(
                test::scheduler::co_block_done_immediately_stacked_outer_T(
                    t,
                    co_ids_identical, 
                    thd_id));

            HCE_INFO_LOG("stacked block done immediately join 2");
            auto awt2 = sch->schedule(
                test::scheduler::co_block_done_immediately_stacked_outer_T(
                    t,
                    co_ids_identical2, 
                    thd_id));

            HCE_INFO_LOG("stacked block done immediately join 3");
            auto awt3 = sch->schedule(
                test::scheduler::co_block_done_immediately_stacked_outer_T(
                    t,
                    co_ids_identical3, 
                    thd_id));

            EXPECT_EQ(t, (T)std::move(awt));
            EXPECT_FALSE(co_ids_identical);
            EXPECT_EQ(t, (T)std::move(awt2));
            EXPECT_FALSE(co_ids_identical2);
            EXPECT_EQ(t, (T)std::move(awt3));
            EXPECT_FALSE(co_ids_identical3);
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
            EXPECT_EQ(0, sch->block_worker_count());
        };

        try {
            schedule_blocking_co((T)test::init<T>(3));
            schedule_blocking_co((T)test::init<T>(2));
            schedule_blocking_co((T)test::init<T>(1));

            ++success_count;
        } catch(const std::exception& e) {
            LOG_F(ERROR, e.what());
        }
        HCE_INFO_LOG("coroutine stacked block done immediately-");
    }

    {
        HCE_INFO_LOG("coroutine stacked block+");
        test::queue<T> q;
        auto lf = hce::scheduler::make();
        std::shared_ptr<hce::scheduler> sch = lf->scheduler();

        auto schedule_blocking_co = [&](T t) {
            auto thd_id = std::this_thread::get_id();
            bool co_ids_identical = true;
            bool co_ids_identical2 = true;
            bool co_ids_identical3 = true;

            HCE_INFO_LOG("co stacked block for queue join 1");
            auto awt = sch->schedule(
                test::scheduler::co_block_for_queue_stacked_outer_T(
                    q,
                    co_ids_identical, 
                    thd_id));

            HCE_INFO_LOG("co stacked block for queue join 2");
            auto awt2 = sch->schedule(
                test::scheduler::co_block_for_queue_stacked_outer_T(
                    q,
                    co_ids_identical2, 
                    thd_id));

            HCE_INFO_LOG("co stacked block for queue join 3");
            auto awt3 = sch->schedule(
                test::scheduler::co_block_for_queue_stacked_outer_T(
                    q,
                    co_ids_identical3, 
                    thd_id));

            std::this_thread::sleep_for(std::chrono::milliseconds(40));
            EXPECT_EQ(3, sch->block_worker_count());
            q.push(t);
            q.push(t);
            q.push(t);
            EXPECT_EQ(t, (T)std::move(awt));
            EXPECT_FALSE(co_ids_identical);
            EXPECT_EQ(t, (T)std::move(awt2));
            EXPECT_FALSE(co_ids_identical2);
            EXPECT_EQ(t, (T)std::move(awt3));
            EXPECT_FALSE(co_ids_identical3);
        };

        try {
            schedule_blocking_co((T)test::init<T>(3));
            schedule_blocking_co((T)test::init<T>(2));
            schedule_blocking_co((T)test::init<T>(1));

            ++success_count;
        } catch(const std::exception& e) {
            LOG_F(ERROR, e.what());
        }
        HCE_INFO_LOG("coroutine stacked block-");
    }

    return success_count;
}

}
}

TEST(scheduler, block_and_block_worker) {
    const size_t expected = 8;
    EXPECT_EQ(expected, test::scheduler::block_T<int>());
    EXPECT_EQ(expected, test::scheduler::block_T<unsigned int>());
    EXPECT_EQ(expected, test::scheduler::block_T<size_t>());
    EXPECT_EQ(expected, test::scheduler::block_T<float>());
    EXPECT_EQ(expected, test::scheduler::block_T<double>());
    EXPECT_EQ(expected, test::scheduler::block_T<char>());
    EXPECT_EQ(expected, test::scheduler::block_T<void*>());
    EXPECT_EQ(expected, test::scheduler::block_T<std::string>());
    EXPECT_EQ(expected, test::scheduler::block_T<test::CustomObject>());
}

namespace test {
namespace scheduler {

template <typename T>
T block_for_queue_simple_T(test::queue<T>& q) {
    return q.pop();
}

template <typename T>
hce::co<T> co_block_for_queue_simple_T(test::queue<T>& q) {
    HCE_INFO_FUNCTION_BODY("co_block_for_queue_simple_T",hce::coroutine::local());
    T result = co_await hce::scheduler::get().block(block_for_queue_simple_T<T>,std::ref(q));
    co_return std::move(result);
}

template <typename T>
size_t block_worker_pool_limit_T(const size_t pool_limit) {
    size_t success_count = 0;

    for(size_t reuse_cnt=0; reuse_cnt<pool_limit; ++reuse_cnt) {
        std::vector<test::queue<T>> q(pool_limit);
        auto cfg = hce::scheduler::config::make();
        cfg->block_worker_pool_limit = reuse_cnt;
        auto lf = hce::scheduler::make(std::move(cfg));
        std::shared_ptr<hce::scheduler> sch = lf->scheduler();

        std::deque<hce::awt<T>> awts;

        try {
            EXPECT_EQ(reuse_cnt, sch->block_worker_pool_limit());
            EXPECT_EQ(0, sch->block_worker_count());

            for(size_t i=0; i<pool_limit; ++i) {
                awts.push_back(sch->schedule(
                    test::scheduler::co_block_for_queue_simple_T(q[i])));
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(40));

            EXPECT_EQ(reuse_cnt, sch->block_worker_pool_limit());
            EXPECT_EQ(pool_limit, sch->block_worker_count());
            
            for(size_t i=0; i<pool_limit; ++i) {
                q[i].push((T)test::init<T>(i));
            }

            for(size_t i=0; i<pool_limit; ++i) {
                EXPECT_EQ((T)test::init<T>(i), (T)std::move(awts.front()));
                awts.pop_front();
            }

            EXPECT_EQ(reuse_cnt, sch->block_worker_pool_limit());
            EXPECT_EQ(reuse_cnt, sch->block_worker_count());

            ++success_count;
        } catch(const std::exception& e) {
            LOG_F(ERROR, e.what());
        }
    }

    return success_count;
}

}
}

TEST(scheduler, block_worker_and_block_worker_pool_limit) {
    const size_t expected = 10;
    EXPECT_EQ(expected, test::scheduler::block_worker_pool_limit_T<int>(10));
    EXPECT_EQ(expected, test::scheduler::block_worker_pool_limit_T<unsigned int>(10));
    EXPECT_EQ(expected, test::scheduler::block_worker_pool_limit_T<size_t>(10));
    EXPECT_EQ(expected, test::scheduler::block_worker_pool_limit_T<float>(10));
    EXPECT_EQ(expected, test::scheduler::block_worker_pool_limit_T<double>(10));
    EXPECT_EQ(expected, test::scheduler::block_worker_pool_limit_T<char>(10));
    EXPECT_EQ(expected, test::scheduler::block_worker_pool_limit_T<void*>(10));
    EXPECT_EQ(expected, test::scheduler::block_worker_pool_limit_T<std::string>(10));
    EXPECT_EQ(expected, test::scheduler::block_worker_pool_limit_T<test::CustomObject>(10));
}
