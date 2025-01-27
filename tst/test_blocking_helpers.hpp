//SPDX-License-Identifier: Apache-2.0
//Author: Blayne Dennis 
#ifndef __HCE_COROUTINE_ENGINE_TEST_BLOCKING_HELPERS__
#define __HCE_COROUTINE_ENGINE_TEST_BLOCKING_HELPERS__

namespace test {
namespace blocking {

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
    hce::awt<T> awt = hce::block(block_done_immediately_T<T>,std::move(t),std::ref(sub_ids_identical), parent_id);
    HCE_INFO_FUNCTION_BODY("co_block_done_immediately_T", "received awt:", awt);
    T result = co_await std::move(awt);
    HCE_INFO_FUNCTION_BODY("co_block_done_immediately_T", "completed awt, result: ", result);
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
T block_for_queue_simple_T(test::queue<T>& q) {
    return q.pop();
}

template <typename T>
hce::co<T> co_block_for_queue_simple_T(test::queue<T>& q) {
    HCE_HIGH_FUNCTION_BODY("co_block_for_queue_simple_T",hce::coroutine::local());
    co_return co_await hce::block(block_for_queue_simple_T<T>,std::ref(q));
}

}
}

#endif
