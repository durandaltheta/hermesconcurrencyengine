//SPDX-License-Identifier: Apache-2.0
//Author: Blayne Dennis 

#include <filesystem>
#include <string>
#include <unistd.h>

#include "hce.hpp"

#include <gtest/gtest.h> 
#include "test_module_helpers.hpp"

std::filesystem::path get_executable_path() {
    return std::filesystem::canonical("/proc/self/exe");
}

namespace test {
namespace module {

hce::co<bool> concurrent_communication_op(test::module::interface* intf) {
    // need to flip send/receive order compared to module_ut_shared.cpp 
    co_return (co_await intf->conc_chs.send()) && (co_await intf->conc_chs.receive());
}

hce::co<bool> parallel_communication_op(test::module::interface* intf) {
    // need to flip send/receive order compared to module_ut_shared.cpp 
    co_return (co_await intf->para_chs.send()) && (co_await intf->para_chs.receive());
}

}
}

TEST(module, import) {
    // put the test objects on the host stack
    test::module::interface intf;

    // import the module and get its awaitable
    const std::string so_lib_name = "module_ut_shared.so";
    std::filesystem::path so_lib_path = get_executable_path().parent_path() / so_lib_name;
    auto awt_module = hce::module::import(so_lib_path.c_str(), (void*)&intf);

    // blocks until all concurrent sends and receives finish
    hce::schedule(concurrent_communication_op(&intf)); 

    // blocks until all parallel sends and receives finish
    hce::schedule(parallel_communication_op(&intf)); 

    // wait till timer tests start
    intf.tmr.wait_for_start();
    auto start = hce::chrono::now();

    // wait till timer tests end
    intf.tmr.wait_for_end();
    auto dur = hce::chrono::now() - start;

    // make sure the timer tests took a reasonable amount of total time
    EXPECT_GT(dur, test::module::timer::expected_minimum_timeouts());
    EXPECT_LT(dur, test::module::timer::expected_total_timeouts());

    // join with module and get the return code
    EXPECT_EQ(test::module::interface::expected_code, (int)awt_module);

    // validate everything
    EXPECT_TRUE(intf.conc_chs.validate_results());
    EXPECT_TRUE(intf.para_chs.validate_results());
    EXPECT_TRUE(intf.blk.validate_results());
    EXPECT_TRUE(intf.tmr.validate_results());
}
