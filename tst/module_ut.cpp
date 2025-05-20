//SPDX-License-Identifier: Apache-2.0
//Author: Blayne Dennis 

#include <filesystem>
#include <string>
#include <unistd.h>

#include "hce.hpp"

#include <gtest/gtest.h> 
#include "test_module_helpers.hpp"

namespace test {
namespace module {

hce::co<bool> conc_send_op(test::module::interface* intf, const char* owner) {
    co_return co_await intf->cchs.send(owner);
}

hce::co<bool> para_send_op(test::module::interface* intf, const char* owner) {
    co_return co_await intf->pchs.send(owner);
}

hce::co<bool> conc_receive_op(test::module::interface* intf, const char* owner) {
    co_return co_await intf->cchs.receive(owner);
}

hce::co<bool> para_receive_op(test::module::interface* intf, const char* owner) {
    co_return co_await intf->pchs.receive(owner);
}

}
}

struct module : public ::testing::Test {
    static const std::string fname;
    static const std::string so_lib_name;
    static const std::filesystem::path so_lib_path;
    static hce::awt<int> awt;

    static std::filesystem::path get_module_path() {
        return std::filesystem::canonical("/proc/self/exe").parent_path() / 
            module::so_lib_name;
    }

protected:
    // This function is called before each test.
    void SetUp() override {
        HCE_INFO_FUNCTION_BODY(module::fname, "init");
        test::module::interface::global().init();
        HCE_INFO_FUNCTION_BODY(module::fname, "import");
        awt = hce::module::import(module::so_lib_path.c_str(), (void*)&(test::module::interface::global()));
    }

    // This function is called after each test.
    void TearDown() override {
        HCE_INFO_FUNCTION_BODY(module::fname, "shutdown command channel");
        test::module::interface::global().comch.close();
        HCE_INFO_FUNCTION_BODY(module::fname, "join module");
        EXPECT_EQ(test::module::interface::expected_code, (int)module::awt);
        HCE_INFO_FUNCTION_BODY(module::fname, "done");
    }
};

const std::string module::fname = "host";
const std::string module::so_lib_name = "libmodule_ut_shared.so";
const std::filesystem::path module::so_lib_path = module::get_module_path();
hce::awt<int> module::awt;

TEST_F(module, empty_result_sanity) {
    {
        auto result = test::module::interface::global().cchs.validate_results();
        EXPECT_EQ(5400, result.expected);
        EXPECT_EQ(0, result.actual);
        EXPECT_EQ(0, result.error);
    }

    {
        auto result = test::module::interface::global().pchs.validate_results();
        EXPECT_EQ(5400, result.expected);
        EXPECT_EQ(0, result.actual);
        EXPECT_EQ(0, result.error);
    }

    {
        auto result = test::module::interface::global().blk.validate_results();
        EXPECT_EQ(2700, result.expected);
        EXPECT_EQ(0, result.actual);
        EXPECT_EQ(0, result.error);
    }

    {
        auto result = test::module::interface::global().tmr.validate_results();
        EXPECT_EQ(20, result.expected);
        EXPECT_EQ(0, result.actual);
        EXPECT_EQ(0, result.error);
    }
}

TEST_F(module, send_concurrent) {
    HCE_INFO_FUNCTION_BODY(module::fname, "send command: send_concurrent_req");
    EXPECT_TRUE((bool)test::module::interface::global().comch.send(test::module::command::send_concurrent_req));
    hce::schedule(conc_receive_op(&test::module::interface::global(), module::fname.c_str())); 
    bool success = false;
    EXPECT_TRUE((bool)test::module::interface::global().resch.recv(success));
    EXPECT_TRUE(success);

    auto result = test::module::interface::global().cchs.validate_results();
    EXPECT_TRUE(result);
    EXPECT_EQ(5400, result.expected);
    EXPECT_EQ(5400, result.actual);
    EXPECT_EQ(0, result.error);

    HCE_INFO_FUNCTION_BODY(module::fname, "done");
}

TEST_F(module, receive_concurrent) {
    HCE_INFO_FUNCTION_BODY(module::fname, "send command: receive_concurrent_req");
    test::module::interface::global().cchs.reset_results();
    EXPECT_TRUE((bool)test::module::interface::global().comch.send(test::module::command::receive_concurrent_req));
    hce::schedule(conc_send_op(&test::module::interface::global(), module::fname.c_str())); 
    bool success = false;
    EXPECT_TRUE((bool)test::module::interface::global().resch.recv(success));
    EXPECT_TRUE(success);

    auto result = test::module::interface::global().cchs.validate_results();
    EXPECT_TRUE(result);
    EXPECT_EQ(5400, result.expected);
    EXPECT_EQ(5400, result.actual);
    EXPECT_EQ(0, result.error);
}

TEST_F(module, send_parallel) {
    HCE_INFO_FUNCTION_BODY(module::fname, "send command: send_parallel_req");
    EXPECT_TRUE((bool)test::module::interface::global().comch.send(test::module::command::send_parallel_req));
    hce::schedule(para_receive_op(&test::module::interface::global(), module::fname.c_str())); 
    bool success = false;
    EXPECT_TRUE((bool)test::module::interface::global().resch.recv(success));
    EXPECT_TRUE(success);

    auto result = test::module::interface::global().pchs.validate_results();
    EXPECT_TRUE(result);
    EXPECT_EQ(5400, result.expected);
    EXPECT_EQ(5400, result.actual);
    EXPECT_EQ(0, result.error);
}

TEST_F(module, receive_parallel) {
    HCE_INFO_FUNCTION_BODY(module::fname, "send command: receive_parallel_req");
    EXPECT_TRUE((bool)test::module::interface::global().comch.send(test::module::command::receive_parallel_req));
    hce::schedule(para_send_op(&test::module::interface::global(), module::fname.c_str())); 
    bool success = false;
    EXPECT_TRUE((bool)test::module::interface::global().resch.recv(success));
    EXPECT_TRUE(success);

    auto result = test::module::interface::global().pchs.validate_results();
    EXPECT_TRUE(result);
    EXPECT_EQ(5400, result.expected);
    EXPECT_EQ(5400, result.actual);
    EXPECT_EQ(0, result.error);
}

TEST_F(module, blocking) {
    HCE_INFO_FUNCTION_BODY(module::fname, "send command: blocking_req");
    EXPECT_TRUE((bool)test::module::interface::global().comch.send(test::module::command::blocking_req));
    bool success = false;
    EXPECT_TRUE((bool)test::module::interface::global().resch.recv(success));
    EXPECT_TRUE(success);

    auto result = test::module::interface::global().blk.validate_results();
    EXPECT_TRUE(result);
    EXPECT_EQ(2700, result.expected);
    EXPECT_EQ(2700, result.actual);
    EXPECT_EQ(0, result.error);
}

TEST_F(module, timing){
    HCE_INFO_FUNCTION_BODY(module::fname, "send command: timing_req");
    EXPECT_TRUE((bool)test::module::interface::global().comch.send(test::module::command::timing_req));

    HCE_INFO_FUNCTION_BODY(module::fname, "wait for timer start");
    // wait till timer tests start
    test::module::interface::global().tmr.wait_for_start();
    auto start = hce::chrono::now();

    HCE_INFO_FUNCTION_BODY(module::fname, "wait for timer end");
    // wait till timer tests end
    test::module::interface::global().tmr.wait_for_end();
    auto dur = hce::chrono::now() - start;

    // make sure the timer tests took a reasonable amount of total time
    EXPECT_GT(dur, test::module::timer::expected_minimum_timeouts());
    EXPECT_LT(dur, test::module::timer::expected_total_timeouts());
    bool success = false;
    EXPECT_TRUE((bool)test::module::interface::global().resch.recv(success));
    EXPECT_TRUE(success);

    auto result = test::module::interface::global().tmr.validate_results();
    EXPECT_TRUE(result);
    EXPECT_EQ(20, result.expected);
    EXPECT_EQ(20, result.actual);
    EXPECT_EQ(0, result.error);
}
