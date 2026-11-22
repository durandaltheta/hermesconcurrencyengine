//SPDX-License-Identifier: Apache-2.0
//Author: Blayne Dennis 
#pragma once 

#include <deque>
#include <string>
#include <thread>
#include <chrono>
#include <vector>
#include <algorithm>

#include "hce/atomic.hpp"
#include "hce/scheduler.hpp"
#include "hce/blocking.hpp"

#include <gtest/gtest.h> 
#include "test_helpers.hpp"

namespace test {
namespace blocking {

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
    T result = hce::block(block_done_immediately_T<T>,std::move(t), std::ref(sub_ids_identical), thd_id);
    EXPECT_TRUE(sub_ids_identical);
    return result;
}

void block_done_immediately_stacked_outer_void(bool& ids_identical, std::thread::id parent_id) {
    auto thd_id = std::this_thread::get_id();
    ids_identical = parent_id == thd_id;
    bool sub_ids_identical = false;
    hce::block(block_done_immediately_void, std::ref(sub_ids_identical), thd_id);
    EXPECT_TRUE(sub_ids_identical);
    return;
}

template <typename T>
hce::co<T> co_block_done_immediately_T(T t, bool& ids_identical, std::thread::id parent_id) {
    auto thd_id = std::this_thread::get_id();
    ids_identical = parent_id == thd_id;
    bool sub_ids_identical = true;
    hce::awt<T> awt = hce::block(block_done_immediately_T<T>,std::move(t),std::ref(sub_ids_identical), parent_id);
    T result = co_await std::move(awt);
    EXPECT_FALSE(sub_ids_identical);
    co_return std::move(result);
}

hce::co<void> co_block_done_immediately_void(bool& ids_identical, std::thread::id parent_id) {
    auto thd_id = std::this_thread::get_id();
    ids_identical = parent_id == thd_id;
    bool sub_ids_identical = true;
    co_await hce::block(block_done_immediately_void,std::ref(sub_ids_identical), parent_id);
    EXPECT_FALSE(sub_ids_identical);
    co_return;
}

template <typename T>
hce::co<T> co_block_done_immediately_stacked_outer_T(T t, bool& ids_identical, std::thread::id parent_id) {
    auto thd_id = std::this_thread::get_id();
    ids_identical = parent_id == thd_id;
    bool sub_ids_identical = true;
    T result = co_await hce::block(block_done_immediately_stacked_outer_T<T>,std::move(t),std::ref(sub_ids_identical), parent_id);
    EXPECT_FALSE(sub_ids_identical);
    co_return std::move(result);
}

hce::co<void> co_block_done_immediately_stacked_outer_void(bool& ids_identical, std::thread::id parent_id) {
    auto thd_id = std::this_thread::get_id();
    ids_identical = parent_id == thd_id;
    bool sub_ids_identical = true;
    co_await hce::block(block_done_immediately_stacked_outer_void,std::ref(sub_ids_identical), parent_id);
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
    T result = hce::block(block_for_queue_T<T>,std::ref(q), std::ref(sub_ids_identical), thd_id);
    EXPECT_TRUE(sub_ids_identical);
    return result;
}

void block_for_queue_stacked_outer_void(test::queue<void>& q, bool& ids_identical, std::thread::id parent_id) {
    auto thd_id = std::this_thread::get_id();
    ids_identical = parent_id == thd_id;
    bool sub_ids_identical = false;
    hce::block(block_for_queue_void,std::ref(q), std::ref(sub_ids_identical), thd_id);
    EXPECT_TRUE(sub_ids_identical);
    return;
}

template <typename T>
hce::co<T> co_block_for_queue_T(test::queue<T>& q, bool& ids_identical, std::thread::id parent_id) {
    auto thd_id = std::this_thread::get_id();
    ids_identical = parent_id == thd_id;
    bool sub_ids_identical = true;
    T result = co_await hce::block(block_for_queue_T<T>,std::ref(q),std::ref(sub_ids_identical), parent_id);
    EXPECT_FALSE(sub_ids_identical);
    co_return std::move(result);
}

hce::co<void> co_block_for_queue_void(test::queue<void>& q, bool& ids_identical, std::thread::id parent_id) {
    auto thd_id = std::this_thread::get_id();
    ids_identical = parent_id == thd_id;
    bool sub_ids_identical = true;
    co_await hce::block(block_for_queue_void,std::ref(q),std::ref(sub_ids_identical), parent_id);
    EXPECT_FALSE(sub_ids_identical);
    co_return;
}

template <typename T>
hce::co<T> co_block_for_queue_stacked_outer_T(test::queue<T>& q, bool& ids_identical, std::thread::id parent_id) {
    auto thd_id = std::this_thread::get_id();
    ids_identical = parent_id == thd_id;
    bool sub_ids_identical = true;
    T result = co_await hce::block(block_for_queue_stacked_outer_T<T>,std::ref(q),std::ref(sub_ids_identical), parent_id);
    EXPECT_FALSE(sub_ids_identical);
    co_return std::move(result);
}

hce::co<void> co_block_for_queue_stacked_outer_void(test::queue<void>& q, bool& ids_identical, std::thread::id parent_id) {
    auto thd_id = std::this_thread::get_id();
    ids_identical = parent_id == thd_id;
    bool sub_ids_identical = true;
    co_await hce::block(block_for_queue_stacked_outer_void,std::ref(q),std::ref(sub_ids_identical), parent_id);
    EXPECT_FALSE(sub_ids_identical);
    co_return;
}

template <typename T>
T block_for_queue_simple_T(test::queue<T>& q) {
    return q.pop();
}

template <typename T>
hce::co<T> co_block_for_queue_simple_T(test::queue<T>& q) {
    co_return co_await hce::block(block_for_queue_simple_T<T>,std::ref(q));
}

}
}
