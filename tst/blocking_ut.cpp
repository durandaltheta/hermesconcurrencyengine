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

namespace test {
namespace scheduler {

template <typename T>
T block_done_immediately_T(T t, bool& ids_identical, std::thread::id parent_id) {
    HCE_HIGH_FUNCTION_BODY("block_done_immediately_T");
    ids_identical = parent_id == std::this_thread::get_id();
    return std::move(t);
}

void block_done_immediately_void(bool& ids_identical, std::thread::id parent_id) {
    HCE_HIGH_FUNCTION_BODY("block_done_immediately_void");
    ids_identical = parent_id == std::this_thread::get_id();
    return;
}

template <typename T>
T block_done_immediately_stacked_outer_T(T t, bool& ids_identical, std::thread::id parent_id) {
    HCE_HIGH_FUNCTION_BODY("block_done_immediately_stacked_outer_T");
    auto thd_id = std::this_thread::get_id();
    ids_identical = parent_id == thd_id;
    bool sub_ids_identical = false;
    T result = hce::block(block_done_immediately_T<T>,std::move(t), std::ref(sub_ids_identical), thd_id);
    EXPECT_TRUE(sub_ids_identical);
    return result;
}

void block_done_immediately_stacked_outer_void(bool& ids_identical, std::thread::id parent_id) {
    HCE_HIGH_FUNCTION_BODY("block_done_immediately_stacked_outer_void");
    auto thd_id = std::this_thread::get_id();
    ids_identical = parent_id == thd_id;
    bool sub_ids_identical = false;
    hce::block(block_done_immediately_void, std::ref(sub_ids_identical), thd_id);
    EXPECT_TRUE(sub_ids_identical);
    return;
}

template <typename T>
hce::co<T> co_block_done_immediately_T(T t, bool& ids_identical, std::thread::id parent_id) {
    HCE_HIGH_FUNCTION_BODY("co_block_done_immediately_T",hce::coroutine::local());
    auto thd_id = std::this_thread::get_id();
    ids_identical = parent_id == thd_id;
    bool sub_ids_identical = true;
    T result = co_await hce::block(block_done_immediately_T<T>,std::move(t),std::ref(sub_ids_identical), parent_id);
    EXPECT_FALSE(sub_ids_identical);
    co_return std::move(result);
}

hce::co<void> co_block_done_immediately_void(bool& ids_identical, std::thread::id parent_id) {
    HCE_HIGH_FUNCTION_BODY("co_block_done_immediately_void",hce::coroutine::local());
    auto thd_id = std::this_thread::get_id();
    ids_identical = parent_id == thd_id;
    bool sub_ids_identical = true;
    co_await hce::block(block_done_immediately_void,std::ref(sub_ids_identical), parent_id);
    EXPECT_FALSE(sub_ids_identical);
    co_return;
}

template <typename T>
hce::co<T> co_block_done_immediately_stacked_outer_T(T t, bool& ids_identical, std::thread::id parent_id) {
    HCE_HIGH_FUNCTION_BODY("co_block_done_immediately_stacked_outer_T",hce::coroutine::local());
    auto thd_id = std::this_thread::get_id();
    ids_identical = parent_id == thd_id;
    bool sub_ids_identical = true;
    T result = co_await hce::block(block_done_immediately_stacked_outer_T<T>,std::move(t),std::ref(sub_ids_identical), parent_id);
    EXPECT_FALSE(sub_ids_identical);
    co_return std::move(result);
}

hce::co<void> co_block_done_immediately_stacked_outer_void(bool& ids_identical, std::thread::id parent_id) {
    HCE_HIGH_FUNCTION_BODY("co_block_done_immediately_stacked_outer_void",hce::coroutine::local());
    auto thd_id = std::this_thread::get_id();
    ids_identical = parent_id == thd_id;
    bool sub_ids_identical = true;
    co_await hce::block(block_done_immediately_stacked_outer_void,std::ref(sub_ids_identical), parent_id);
    EXPECT_FALSE(sub_ids_identical);
    co_return;
}

template <typename T>
T block_for_queue_T(test::queue<T>& q, bool& ids_identical, std::thread::id parent_id) {
    HCE_HIGH_FUNCTION_BODY("block_for_queue_T");
    ids_identical = parent_id == std::this_thread::get_id();
    return q.pop();
}

void block_for_queue_void(test::queue<void>& q, bool& ids_identical, std::thread::id parent_id) {
    HCE_HIGH_FUNCTION_BODY("block_for_queue_void");
    ids_identical = parent_id == std::this_thread::get_id();
    q.pop();
    return;
}

template <typename T>
T block_for_queue_stacked_outer_T(test::queue<T>& q, bool& ids_identical, std::thread::id parent_id) {
    HCE_HIGH_FUNCTION_BODY("block_for_queue_stacked_outer_T");
    auto thd_id = std::this_thread::get_id();
    ids_identical = parent_id == thd_id;
    bool sub_ids_identical = false;
    T result = hce::block(block_for_queue_T<T>,std::ref(q), std::ref(sub_ids_identical), thd_id);
    EXPECT_TRUE(sub_ids_identical);
    return result;
}

void block_for_queue_stacked_outer_void(test::queue<void>& q, bool& ids_identical, std::thread::id parent_id) {
    HCE_HIGH_FUNCTION_BODY("block_for_queue_stacked_outer_void");
    auto thd_id = std::this_thread::get_id();
    ids_identical = parent_id == thd_id;
    bool sub_ids_identical = false;
    hce::block(block_for_queue_void,std::ref(q), std::ref(sub_ids_identical), thd_id);
    EXPECT_TRUE(sub_ids_identical);
    return;
}

template <typename T>
hce::co<T> co_block_for_queue_T(test::queue<T>& q, bool& ids_identical, std::thread::id parent_id) {
    HCE_HIGH_FUNCTION_BODY("co_block_for_queue_T","T: ",typeid(T).name(),", coroutine: ",hce::coroutine::local());
    auto thd_id = std::this_thread::get_id();
    ids_identical = parent_id == thd_id;
    bool sub_ids_identical = true;
    T result = co_await hce::block(block_for_queue_T<T>,std::ref(q),std::ref(sub_ids_identical), parent_id);
    EXPECT_FALSE(sub_ids_identical);
    co_return std::move(result);
}

hce::co<void> co_block_for_queue_void(test::queue<void>& q, bool& ids_identical, std::thread::id parent_id) {
    HCE_HIGH_FUNCTION_BODY("co_block_for_queue_void","coroutine: ",hce::coroutine::local());
    auto thd_id = std::this_thread::get_id();
    ids_identical = parent_id == thd_id;
    bool sub_ids_identical = true;
    co_await hce::block(block_for_queue_void,std::ref(q),std::ref(sub_ids_identical), parent_id);
    EXPECT_FALSE(sub_ids_identical);
    co_return;
}

template <typename T>
hce::co<T> co_block_for_queue_stacked_outer_T(test::queue<T>& q, bool& ids_identical, std::thread::id parent_id) {
    HCE_HIGH_FUNCTION_BODY("co_block_for_queue_stacked_outer_T","T: ",typeid(T).name(),", coroutine: ",hce::coroutine::local());
    auto thd_id = std::this_thread::get_id();
    ids_identical = parent_id == thd_id;
    bool sub_ids_identical = true;
    T result = co_await hce::block(block_for_queue_stacked_outer_T<T>,std::ref(q),std::ref(sub_ids_identical), parent_id);
    EXPECT_FALSE(sub_ids_identical);
    co_return std::move(result);
}

hce::co<void> co_block_for_queue_stacked_outer_void(test::queue<void>& q, bool& ids_identical, std::thread::id parent_id) {
    HCE_HIGH_FUNCTION_BODY("co_block_for_queue_stacked_outer_void",hce::coroutine::local());
    auto thd_id = std::this_thread::get_id();
    ids_identical = parent_id == thd_id;
    bool sub_ids_identical = true;
    co_await hce::block(block_for_queue_stacked_outer_void,std::ref(q),std::ref(sub_ids_identical), parent_id);
    EXPECT_FALSE(sub_ids_identical);
    co_return;
}

template <typename T>
size_t block_T() {
    const std::string fname = hce::type::templatize<T>("block_T");
    size_t success_count = 0;
    const size_t process_worker_resource_limit = 
        hce::blocking::config::process_worker_resource_limit();

    auto post_block_expected_worker_count = [&](size_t blocks_executed) {
        return process_worker_resource_limit < blocks_executed
            ? process_worker_resource_limit
            : blocks_executed;
    };

    {
        HCE_INFO_FUNCTION_BODY(fname,"thread block done immediately");
        auto schedule_blocking = [&](T t) {
            hce::blocking::service::get().clear_workers();
            auto thd_id = std::this_thread::get_id();
            bool ids_identical = false;
            bool ids_identical2 = false;
            bool ids_identical3 = false;
            EXPECT_EQ(0, hce::blocking::service::get().worker_count());
            EXPECT_EQ(t, (T)hce::block(block_done_immediately_T<T>,t,std::ref(ids_identical), thd_id));
            EXPECT_TRUE(ids_identical);
            EXPECT_EQ(t, (T)hce::block(block_done_immediately_T<T>,t,std::ref(ids_identical2), thd_id));
            EXPECT_TRUE(ids_identical2);
            EXPECT_EQ(t, (T)hce::block(block_done_immediately_T<T>,t,std::ref(ids_identical3), thd_id));
            EXPECT_TRUE(ids_identical3);
            EXPECT_EQ(0, hce::blocking::service::get().worker_count());
        };

        try {
            schedule_blocking((T)test::init<T>(3));
            schedule_blocking((T)test::init<T>(2));
            schedule_blocking((T)test::init<T>(1));

            ++success_count;
        } catch(const std::exception& e) {
            LOG_F(ERROR, e.what());
        }
    }

    {
        HCE_INFO_FUNCTION_BODY(fname,"thread block for queue");
        auto schedule_blocking = [&](T t) {
            hce::blocking::service::get().clear_workers();
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

            EXPECT_EQ(0, hce::blocking::service::get().worker_count());
            launch_sender_thd();
            launch_sender_thd();
            launch_sender_thd();
            EXPECT_EQ(t, (T)hce::block(block_for_queue_T<T>,std::ref(q),std::ref(ids_identical), thd_id));
            EXPECT_TRUE(ids_identical);
            EXPECT_EQ(t, (T)hce::block(block_for_queue_T<T>,std::ref(q),std::ref(ids_identical2), thd_id));
            EXPECT_TRUE(ids_identical2);
            EXPECT_EQ(t, (T)hce::block(block_for_queue_T<T>,std::ref(q),std::ref(ids_identical3), thd_id));
            EXPECT_TRUE(ids_identical3);
            EXPECT_EQ(0, hce::blocking::service::get().worker_count());
        };

        try {
            schedule_blocking((T)test::init<T>(3));
            schedule_blocking((T)test::init<T>(2));
            schedule_blocking((T)test::init<T>(1));

            ++success_count;
        } catch(const std::exception& e) {
            LOG_F(ERROR, e.what());
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
            hce::blocking::service::get().clear_workers();
            auto thd_id = std::this_thread::get_id();
            bool ids_identical = false;
            bool ids_identical2 = false;
            bool ids_identical3 = false;
            EXPECT_EQ(0, hce::blocking::service::get().worker_count());
            EXPECT_EQ(t, (T)hce::block(block_done_immediately_stacked_outer_T<T>,t,std::ref(ids_identical), thd_id));
            EXPECT_TRUE(ids_identical);
            EXPECT_EQ(t, (T)hce::block(block_done_immediately_stacked_outer_T<T>,t,std::ref(ids_identical2), thd_id));
            EXPECT_TRUE(ids_identical2);
            EXPECT_EQ(t, (T)hce::block(block_done_immediately_stacked_outer_T<T>,t,std::ref(ids_identical3), thd_id));
            EXPECT_TRUE(ids_identical3);

            EXPECT_EQ(0, hce::blocking::service::get().worker_count());
        };

        try {
            schedule_blocking((T)test::init<T>(3));
            schedule_blocking((T)test::init<T>(2));
            schedule_blocking((T)test::init<T>(1));

            ++success_count;
        } catch(const std::exception& e) {
            LOG_F(ERROR, e.what());
        }
    }

    {
        HCE_INFO_FUNCTION_BODY(fname,"thread stacked block");
        auto schedule_blocking = [&](T t) {
            hce::blocking::service::get().clear_workers();
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

            EXPECT_EQ(0, hce::blocking::service::get().worker_count());
            launch_sender_thd();
            launch_sender_thd();
            launch_sender_thd();
            EXPECT_EQ(t, (T)hce::block(block_for_queue_stacked_outer_T<T>,std::ref(q),std::ref(ids_identical), thd_id));
            EXPECT_TRUE(ids_identical);
            EXPECT_EQ(t, (T)hce::block(block_for_queue_stacked_outer_T<T>,std::ref(q),std::ref(ids_identical2), thd_id));
            EXPECT_TRUE(ids_identical2);
            EXPECT_EQ(t, (T)hce::block(block_for_queue_stacked_outer_T<T>,std::ref(q),std::ref(ids_identical3), thd_id));
            EXPECT_TRUE(ids_identical3);
            
            EXPECT_EQ(0, hce::blocking::service::get().worker_count());
        };

        try {
            schedule_blocking((T)test::init<T>(3));
            schedule_blocking((T)test::init<T>(2));
            schedule_blocking((T)test::init<T>(1));

            ++success_count;
        } catch(const std::exception& e) {
            LOG_F(ERROR, e.what());
        }
    }

    {
        HCE_INFO_FUNCTION_BODY(fname,"coroutine block done immediately");
        test::queue<T> q;

        auto schedule_blocking_co = [&](T t) {
            hce::blocking::service::get().clear_workers();
            auto lf = hce::scheduler::make();
            std::shared_ptr<hce::scheduler> sch = lf->scheduler();
            auto thd_id = std::this_thread::get_id();
            bool co_ids_identical = true;
            bool co_ids_identical2 = true;
            bool co_ids_identical3 = true;

            auto awt = sch->schedule(
                test::scheduler::co_block_done_immediately_T(
                    t,
                    co_ids_identical, 
                    thd_id));

            auto awt2 = sch->schedule(
                test::scheduler::co_block_done_immediately_T(
                    t,
                    co_ids_identical2, 
                    thd_id));

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

            EXPECT_EQ(post_block_expected_worker_count(3), hce::blocking::service::get().worker_count());
        };

        try {
            schedule_blocking_co((T)test::init<T>(3));
            schedule_blocking_co((T)test::init<T>(2));
            schedule_blocking_co((T)test::init<T>(1));

            ++success_count;
        } catch(const std::exception& e) {
            LOG_F(ERROR, e.what());
        }
    }

    {
        HCE_INFO_FUNCTION_BODY(fname,"coroutine block for queue");
        test::queue<T> q;

        auto schedule_blocking_co = [&](T t) {
            hce::blocking::service::get().clear_workers();
            auto lf = hce::scheduler::make();
            std::shared_ptr<hce::scheduler> sch = lf->scheduler();
            auto thd_id = std::this_thread::get_id();
            bool co_ids_identical = true;
            bool co_ids_identical2 = true;
            bool co_ids_identical3 = true;

            auto awt = sch->schedule(
                test::scheduler::co_block_for_queue_T(
                    q,
                    co_ids_identical, 
                    thd_id));

            auto awt2 = sch->schedule(
                test::scheduler::co_block_for_queue_T(
                    q,
                    co_ids_identical2, 
                    thd_id));

            auto awt3 = sch->schedule(
                test::scheduler::co_block_for_queue_T(
                    q,
                    co_ids_identical3, 
                    thd_id));

            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            EXPECT_EQ(3, hce::blocking::service::get().worker_count());
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
    }

    {
        HCE_INFO_FUNCTION_BODY(fname,"coroutine stacked block done immediately");
        test::queue<T> q;

        auto schedule_blocking_co = [&](T t) {
            hce::blocking::service::get().clear_workers();
            auto lf = hce::scheduler::make();
            std::shared_ptr<hce::scheduler> sch = lf->scheduler();
            auto thd_id = std::this_thread::get_id();
            bool co_ids_identical = true;
            bool co_ids_identical2 = true;
            bool co_ids_identical3 = true;

            auto awt = sch->schedule(
                test::scheduler::co_block_done_immediately_stacked_outer_T(
                    t,
                    co_ids_identical, 
                    thd_id));

            auto awt2 = sch->schedule(
                test::scheduler::co_block_done_immediately_stacked_outer_T(
                    t,
                    co_ids_identical2, 
                    thd_id));

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
            std::this_thread::sleep_for(std::chrono::milliseconds(50));

            EXPECT_EQ(post_block_expected_worker_count(3), hce::blocking::service::get().worker_count());
        };

        try {
            schedule_blocking_co((T)test::init<T>(3));
            schedule_blocking_co((T)test::init<T>(2));
            schedule_blocking_co((T)test::init<T>(1));

            ++success_count;
        } catch(const std::exception& e) {
            LOG_F(ERROR, e.what());
        }
    }

    {
        HCE_INFO_FUNCTION_BODY(fname,"coroutine stacked block");
        test::queue<T> q;
        auto lf = hce::scheduler::make();
        std::shared_ptr<hce::scheduler> sch = lf->scheduler();

        auto schedule_blocking_co = [&](T t) {
            hce::blocking::service::get().clear_workers();
            auto thd_id = std::this_thread::get_id();
            bool co_ids_identical = true;
            bool co_ids_identical2 = true;
            bool co_ids_identical3 = true;

            auto awt = sch->schedule(
                test::scheduler::co_block_for_queue_stacked_outer_T(
                    q,
                    co_ids_identical, 
                    thd_id));

            auto awt2 = sch->schedule(
                test::scheduler::co_block_for_queue_stacked_outer_T(
                    q,
                    co_ids_identical2, 
                    thd_id));

            auto awt3 = sch->schedule(
                test::scheduler::co_block_for_queue_stacked_outer_T(
                    q,
                    co_ids_identical3, 
                    thd_id));

            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            EXPECT_EQ(3, hce::blocking::service::get().worker_count());
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
    }

    return success_count;
}

}
}

TEST(blocking, block_and_block_worker) {
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
    HCE_HIGH_FUNCTION_BODY("co_block_for_queue_simple_T",hce::coroutine::local());
    co_return co_await hce::block(block_for_queue_simple_T<T>,std::ref(q));
}

template <typename T>
size_t block_worker_resource_limit_T(const size_t resource_limit) {
    std::string fname(hce::type::templatize<T>("block_worker_resource_limit_T"));
    HCE_INFO_FUNCTION_ENTER(fname, resource_limit);
    size_t success_count = 0;
    const size_t process_worker_resource_limit = 
        hce::blocking::config::process_worker_resource_limit();

    EXPECT_EQ(process_worker_resource_limit, 
              hce::blocking::service::get().worker_resource_limit());

    for(size_t reuse_cnt=0; reuse_cnt<resource_limit; ++reuse_cnt) {
        HCE_INFO_FUNCTION_BODY(fname, "loop; reuse_cnt:",reuse_cnt);
        std::vector<test::queue<T>> q(resource_limit);
        auto cfg = hce::scheduler::config::make();
        auto lf = hce::scheduler::make(std::move(cfg));
        std::shared_ptr<hce::scheduler> sch = lf->scheduler();

        std::deque<hce::awt<T>> awts;

        try {
            hce::blocking::service::get().clear_workers();

            EXPECT_EQ(0, hce::blocking::service::get().worker_count());

            // block a set of coroutines
            for(size_t i=0; i<resource_limit; ++i) {
                awts.push_back(sch->schedule(
                    test::scheduler::co_block_for_queue_simple_T(q[i])));
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            EXPECT_EQ(resource_limit, hce::blocking::service::get().worker_count());

            // unblock all the coroutines
            for(size_t i=0; i<resource_limit; ++i) {
                q[i].push((T)test::init<T>(i));
            }

            // join all the coroutines
            for(size_t i=0; i<resource_limit; ++i) {
                EXPECT_EQ((T)test::init<T>(i), (T)std::move(awts.front()));
                awts.pop_front();
            }

            // worker count in the cache should have grown to this
            size_t expected_count = std::min(resource_limit, process_worker_resource_limit);
            EXPECT_EQ(expected_count, hce::blocking::service::get().worker_count());

            ++success_count;
        } catch(const std::exception& e) {
            LOG_F(ERROR, e.what());
        }
    }

    return success_count;
}

}
}

TEST(blocking, block_worker_and_block_worker_resource_limit) {
    const size_t expected = 10;
    EXPECT_EQ(expected, test::scheduler::block_worker_resource_limit_T<int>(10));
    EXPECT_EQ(expected, test::scheduler::block_worker_resource_limit_T<unsigned int>(10));
    EXPECT_EQ(expected, test::scheduler::block_worker_resource_limit_T<size_t>(10));
    EXPECT_EQ(expected, test::scheduler::block_worker_resource_limit_T<float>(10));
    EXPECT_EQ(expected, test::scheduler::block_worker_resource_limit_T<double>(10));
    EXPECT_EQ(expected, test::scheduler::block_worker_resource_limit_T<char>(10));
    EXPECT_EQ(expected, test::scheduler::block_worker_resource_limit_T<void*>(10));
    EXPECT_EQ(expected, test::scheduler::block_worker_resource_limit_T<std::string>(10));
    EXPECT_EQ(expected, test::scheduler::block_worker_resource_limit_T<test::CustomObject>(10));
}
