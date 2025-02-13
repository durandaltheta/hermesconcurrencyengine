//SPDX-License-Identifier: Apache-2.0
//Author: Blayne Dennis 
#include <deque>
#include <string>
#include <thread>
#include <chrono>
#include <vector>
#include <algorithm>

#include "loguru.hpp"
#include "atomic.hpp"
#include "scheduler.hpp"
#include "blocking.hpp"

#include <gtest/gtest.h> 
#include "test_helpers.hpp"
#include "test_blocking_helpers.hpp"

namespace test {
namespace blocking {

template <typename T>
size_t block_T() {
    const bool worker_count_check = HCETESTENABLETIMESENSITIVE;
    const std::string fname = hce::type::templatize<T>("block_T");
    size_t success_count = 0;
    const size_t reusable_block_worker_cache_size = 
        hce::config::blocking::reusable_block_worker_cache_size();

    auto post_block_expected_worker_count = [&](size_t blocks_executed) {
        return reusable_block_worker_cache_size < blocks_executed
            ? reusable_block_worker_cache_size
            : blocks_executed;
    };

    {
        HCE_INFO_FUNCTION_BODY(fname,"thread block done immediately");
        auto schedule_blocking = [&](T t) {
            hce::blocking::service::get().clear_worker_cache();
            auto thd_id = std::this_thread::get_id();
            bool ids_identical = false;
            bool ids_identical2 = false;
            bool ids_identical3 = false;

            if(worker_count_check) {
                EXPECT_EQ(0, hce::blocking::service::get().worker_count());
            }

            EXPECT_EQ(t, (T)hce::block(block_done_immediately_T<T>,t,std::ref(ids_identical), thd_id));
            EXPECT_TRUE(ids_identical);
            EXPECT_EQ(t, (T)hce::block(block_done_immediately_T<T>,t,std::ref(ids_identical2), thd_id));
            EXPECT_TRUE(ids_identical2);
            EXPECT_EQ(t, (T)hce::block(block_done_immediately_T<T>,t,std::ref(ids_identical3), thd_id));
            EXPECT_TRUE(ids_identical3);

            if(worker_count_check) {
                EXPECT_EQ(0, hce::blocking::service::get().worker_count());
            }
        };

        try {
            schedule_blocking((T)test::init<T>(3));
            schedule_blocking((T)test::init<T>(2));
            schedule_blocking((T)test::init<T>(1));

            ++success_count;
        } catch(const std::exception& e) {
            LOG_F(ERROR, "%s", e.what());
        }
    }

    {
        HCE_INFO_FUNCTION_BODY(fname,"thread block for queue");
        auto schedule_blocking = [&](T t) {
            hce::blocking::service::get().clear_worker_cache();
            test::queue<T> q;
            auto thd_id = std::this_thread::get_id();
            bool ids_identical = false;
            bool ids_identical2 = false;
            bool ids_identical3 = false;

            auto launch_sender_thd = [&]{
                std::thread([&](T t){
                    EXPECT_EQ(0, hce::blocking::service::get().worker_count());
                    q.push(std::move(t));
                },t).detach();
            };

            if(worker_count_check) {
                EXPECT_EQ(0, hce::blocking::service::get().worker_count());
            }

            launch_sender_thd();
            launch_sender_thd();
            launch_sender_thd();
            EXPECT_EQ(t, (T)hce::block(block_for_queue_T<T>,std::ref(q),std::ref(ids_identical), thd_id));
            EXPECT_TRUE(ids_identical);
            EXPECT_EQ(t, (T)hce::block(block_for_queue_T<T>,std::ref(q),std::ref(ids_identical2), thd_id));
            EXPECT_TRUE(ids_identical2);
            EXPECT_EQ(t, (T)hce::block(block_for_queue_T<T>,std::ref(q),std::ref(ids_identical3), thd_id));
            EXPECT_TRUE(ids_identical3);

            if(worker_count_check) {
                EXPECT_EQ(0, hce::blocking::service::get().worker_count());
            }
        };

        try {
            schedule_blocking((T)test::init<T>(3));
            schedule_blocking((T)test::init<T>(2));
            schedule_blocking((T)test::init<T>(1));

            ++success_count;
        } catch(const std::exception& e) {
            LOG_F(ERROR, "%s", e.what());
        }
    }

    //
    // thread stacked block done immediately 

    // When block() calls are stacked (block() calls block()), the inner block()
    // call should execute immediately on the current thread, leaving the 
    // 'block_worker_count()' count the same as only calling block() once. 
    {
        HCE_INFO_FUNCTION_BODY(fname,"thread stacked block done immediately");
        auto schedule_blocking = [&](T t) {
            hce::blocking::service::get().clear_worker_cache();
            auto thd_id = std::this_thread::get_id();
            bool ids_identical = false;
            bool ids_identical2 = false;
            bool ids_identical3 = false;

            if(worker_count_check) {
                EXPECT_EQ(0, hce::blocking::service::get().worker_count());
            }

            EXPECT_EQ(t, (T)hce::block(block_done_immediately_stacked_outer_T<T>,t,std::ref(ids_identical), thd_id));
            EXPECT_TRUE(ids_identical);
            EXPECT_EQ(t, (T)hce::block(block_done_immediately_stacked_outer_T<T>,t,std::ref(ids_identical2), thd_id));
            EXPECT_TRUE(ids_identical2);
            EXPECT_EQ(t, (T)hce::block(block_done_immediately_stacked_outer_T<T>,t,std::ref(ids_identical3), thd_id));
            EXPECT_TRUE(ids_identical3);

            if(worker_count_check) {
                EXPECT_EQ(0, hce::blocking::service::get().worker_count());
            }
        };

        try {
            schedule_blocking((T)test::init<T>(3));
            schedule_blocking((T)test::init<T>(2));
            schedule_blocking((T)test::init<T>(1));

            ++success_count;
        } catch(const std::exception& e) {
            LOG_F(ERROR, "%s", e.what());
        }
    }

    {
        HCE_INFO_FUNCTION_BODY(fname,"thread stacked block");
        auto schedule_blocking = [&](T t) {
            hce::blocking::service::get().clear_worker_cache();
            test::queue<T> q;
            auto thd_id = std::this_thread::get_id();
            bool ids_identical = false;
            bool ids_identical2 = false;
            bool ids_identical3 = false;

            auto launch_sender_thd = [&]{
                std::thread([&](T t){
                    EXPECT_EQ(0, hce::blocking::service::get().worker_count());
                    q.push(std::move(t));
                },t).detach();
            };

            if(worker_count_check) {
                EXPECT_EQ(0, hce::blocking::service::get().worker_count());
            }

            launch_sender_thd();
            launch_sender_thd();
            launch_sender_thd();
            EXPECT_EQ(t, (T)hce::block(block_for_queue_stacked_outer_T<T>,std::ref(q),std::ref(ids_identical), thd_id));
            EXPECT_TRUE(ids_identical);
            EXPECT_EQ(t, (T)hce::block(block_for_queue_stacked_outer_T<T>,std::ref(q),std::ref(ids_identical2), thd_id));
            EXPECT_TRUE(ids_identical2);
            EXPECT_EQ(t, (T)hce::block(block_for_queue_stacked_outer_T<T>,std::ref(q),std::ref(ids_identical3), thd_id));
            EXPECT_TRUE(ids_identical3);
            
            if(worker_count_check) {
                EXPECT_EQ(0, hce::blocking::service::get().worker_count());
            }
        };

        try {
            schedule_blocking((T)test::init<T>(3));
            schedule_blocking((T)test::init<T>(2));
            schedule_blocking((T)test::init<T>(1));

            ++success_count;
        } catch(const std::exception& e) {
            LOG_F(ERROR, "%s", e.what());
        }
    }

    {
        HCE_INFO_FUNCTION_BODY(fname,"coroutine block done immediately");
        test::queue<T> q;

        auto schedule_blocking_co = [&](T t) {
            hce::blocking::service::get().clear_worker_cache();
            auto lf = hce::scheduler::make();
            std::shared_ptr<hce::scheduler> sch = lf->get_scheduler();
            auto thd_id = std::this_thread::get_id();
            bool co_ids_identical = true;
            bool co_ids_identical2 = true;
            bool co_ids_identical3 = true;

            auto awt = sch->schedule(
                test::blocking::co_block_done_immediately_T(
                    t,
                    co_ids_identical, 
                    thd_id));

            auto awt2 = sch->schedule(
                test::blocking::co_block_done_immediately_T(
                    t,
                    co_ids_identical2, 
                    thd_id));

            auto awt3 = sch->schedule(
                test::blocking::co_block_done_immediately_T(
                    t,
                    co_ids_identical3, 
                    thd_id));

            EXPECT_EQ(t, (T)std::move(awt));
            EXPECT_FALSE(co_ids_identical);
            EXPECT_EQ(t, (T)std::move(awt2));
            EXPECT_FALSE(co_ids_identical2);
            EXPECT_EQ(t, (T)std::move(awt3));
            EXPECT_FALSE(co_ids_identical3);

            if(worker_count_check) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));

                EXPECT_EQ(
                    post_block_expected_worker_count(3), 
                    hce::blocking::service::get().worker_count());
            }
        };

        try {
            schedule_blocking_co((T)test::init<T>(3));
            schedule_blocking_co((T)test::init<T>(2));
            schedule_blocking_co((T)test::init<T>(1));

            ++success_count;
        } catch(const std::exception& e) {
            LOG_F(ERROR, "%s", e.what());
        }
    }

    {
        HCE_INFO_FUNCTION_BODY(fname,"coroutine block for queue");
        test::queue<T> q;

        auto schedule_blocking_co = [&](T t) {
            hce::blocking::service::get().clear_worker_cache();
            auto lf = hce::scheduler::make();
            std::shared_ptr<hce::scheduler> sch = lf->get_scheduler();
            auto thd_id = std::this_thread::get_id();
            bool co_ids_identical = true;
            bool co_ids_identical2 = true;
            bool co_ids_identical3 = true;

            auto awt = sch->schedule(
                test::blocking::co_block_for_queue_T(
                    q,
                    co_ids_identical, 
                    thd_id));

            auto awt2 = sch->schedule(
                test::blocking::co_block_for_queue_T(
                    q,
                    co_ids_identical2, 
                    thd_id));

            auto awt3 = sch->schedule(
                test::blocking::co_block_for_queue_T(
                    q,
                    co_ids_identical3, 
                    thd_id));

            if(worker_count_check) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                EXPECT_EQ(3, hce::blocking::service::get().worker_count());
            }

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
            LOG_F(ERROR, "%s", e.what());
        }
    }

    {
        HCE_INFO_FUNCTION_BODY(fname,"coroutine stacked block done immediately");
        test::queue<T> q;

        auto schedule_blocking_co = [&](T t) {
            hce::blocking::service::get().clear_worker_cache();
            auto lf = hce::scheduler::make();
            std::shared_ptr<hce::scheduler> sch = lf->get_scheduler();
            auto thd_id = std::this_thread::get_id();
            bool co_ids_identical = true;
            bool co_ids_identical2 = true;
            bool co_ids_identical3 = true;

            auto awt = sch->schedule(
                test::blocking::co_block_done_immediately_stacked_outer_T(
                    t,
                    co_ids_identical, 
                    thd_id));

            auto awt2 = sch->schedule(
                test::blocking::co_block_done_immediately_stacked_outer_T(
                    t,
                    co_ids_identical2, 
                    thd_id));

            auto awt3 = sch->schedule(
                test::blocking::co_block_done_immediately_stacked_outer_T(
                    t,
                    co_ids_identical3, 
                    thd_id));

            EXPECT_EQ(t, (T)std::move(awt));
            EXPECT_FALSE(co_ids_identical);
            EXPECT_EQ(t, (T)std::move(awt2));
            EXPECT_FALSE(co_ids_identical2);
            EXPECT_EQ(t, (T)std::move(awt3));
            EXPECT_FALSE(co_ids_identical3);


            if(worker_count_check) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));

                EXPECT_EQ(post_block_expected_worker_count(3), hce::blocking::service::get().worker_count());
            }
        };

        try {
            schedule_blocking_co((T)test::init<T>(3));
            schedule_blocking_co((T)test::init<T>(2));
            schedule_blocking_co((T)test::init<T>(1));

            ++success_count;
        } catch(const std::exception& e) {
            LOG_F(ERROR, "%s", e.what());
        }
    }

    {
        HCE_INFO_FUNCTION_BODY(fname,"coroutine stacked block");
        test::queue<T> q;
        auto lf = hce::scheduler::make();
        std::shared_ptr<hce::scheduler> sch = lf->get_scheduler();

        auto schedule_blocking_co = [&](T t) {
            hce::blocking::service::get().clear_worker_cache();
            auto thd_id = std::this_thread::get_id();
            bool co_ids_identical = true;
            bool co_ids_identical2 = true;
            bool co_ids_identical3 = true;

            auto awt = sch->schedule(
                test::blocking::co_block_for_queue_stacked_outer_T(
                    q,
                    co_ids_identical, 
                    thd_id));

            auto awt2 = sch->schedule(
                test::blocking::co_block_for_queue_stacked_outer_T(
                    q,
                    co_ids_identical2, 
                    thd_id));

            auto awt3 = sch->schedule(
                test::blocking::co_block_for_queue_stacked_outer_T(
                    q,
                    co_ids_identical3, 
                    thd_id));

            if(worker_count_check) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                EXPECT_EQ(3, hce::blocking::service::get().worker_count());
            }

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
            LOG_F(ERROR, "%s", e.what());
        }
    }

    return success_count;
}

}
}

TEST(blocking, block_and_block_worker) {
    const size_t expected = 8;
    EXPECT_EQ(expected, test::blocking::block_T<int>());
    EXPECT_EQ(expected, test::blocking::block_T<unsigned int>());
    EXPECT_EQ(expected, test::blocking::block_T<size_t>());
    EXPECT_EQ(expected, test::blocking::block_T<float>());
    EXPECT_EQ(expected, test::blocking::block_T<double>());
    EXPECT_EQ(expected, test::blocking::block_T<char>());
    EXPECT_EQ(expected, test::blocking::block_T<void*>());
    EXPECT_EQ(expected, test::blocking::block_T<std::string>());
    EXPECT_EQ(expected, test::blocking::block_T<test::CustomObject>());
}

namespace test {
namespace blocking {

template <typename T>
size_t block_worker_cache_size_T(const size_t cache_size) {
    const bool worker_count_check = HCETESTENABLETIMESENSITIVE;
    std::string fname(hce::type::templatize<T>("block_worker_cache_size_T"));
    HCE_INFO_FUNCTION_ENTER(fname, cache_size);
    size_t success_count = 0;
    const size_t reusable_block_worker_cache_size = 
        hce::config::blocking::reusable_block_worker_cache_size();

    EXPECT_EQ(reusable_block_worker_cache_size, 
              hce::blocking::service::get().worker_cache_size());

    for(size_t reuse_cnt=0; reuse_cnt<cache_size; ++reuse_cnt) {
        HCE_INFO_FUNCTION_BODY(fname, "loop; reuse_cnt:",reuse_cnt);
        std::vector<test::queue<T>> q(cache_size);
        auto lf = hce::scheduler::make();
        std::shared_ptr<hce::scheduler> sch = lf->get_scheduler();

        std::deque<hce::awt<T>> awts;

        try {
            hce::blocking::service::get().clear_worker_cache();

            if(worker_count_check) {
                EXPECT_EQ(0, hce::blocking::service::get().worker_count());
            }

            // block a set of coroutines
            for(size_t i=0; i<cache_size; ++i) {
                awts.push_back(sch->schedule(
                    test::blocking::co_block_for_queue_simple_T(q[i])));
            }

            if(worker_count_check) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));

                EXPECT_EQ(cache_size, hce::blocking::service::get().worker_count());
            }

            // unblock all the coroutines
            for(size_t i=0; i<cache_size; ++i) {
                q[i].push((T)test::init<T>(i));
            }

            // join all the coroutines
            for(size_t i=0; i<cache_size; ++i) {
                EXPECT_EQ((T)test::init<T>(i), (T)std::move(awts.front()));
                awts.pop_front();
            }
            
            if(worker_count_check) {
                // worker count in the cache should have grown to this
                size_t expected_count = std::min(cache_size, reusable_block_worker_cache_size);
                EXPECT_EQ(expected_count, hce::blocking::service::get().worker_count());
            }

            ++success_count;
        } catch(const std::exception& e) {
            LOG_F(ERROR, "%s", e.what());
        }
    }

    return success_count;
}

}
}

TEST(blocking, block_worker_and_block_worker_cache_size) {
    const size_t expected = 10;
    EXPECT_EQ(expected, test::blocking::block_worker_cache_size_T<int>(10));
    EXPECT_EQ(expected, test::blocking::block_worker_cache_size_T<unsigned int>(10));
    EXPECT_EQ(expected, test::blocking::block_worker_cache_size_T<size_t>(10));
    EXPECT_EQ(expected, test::blocking::block_worker_cache_size_T<float>(10));
    EXPECT_EQ(expected, test::blocking::block_worker_cache_size_T<double>(10));
    EXPECT_EQ(expected, test::blocking::block_worker_cache_size_T<char>(10));
    EXPECT_EQ(expected, test::blocking::block_worker_cache_size_T<void*>(10));
    EXPECT_EQ(expected, test::blocking::block_worker_cache_size_T<std::string>(10));
    EXPECT_EQ(expected, test::blocking::block_worker_cache_size_T<test::CustomObject>(10));
}
