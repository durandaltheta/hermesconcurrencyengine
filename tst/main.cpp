#include <gtest/gtest.h>

#include "hce.hpp"
#include "test_module_helpers.hpp"

test::module::interface* g_test_module_intf_ = nullptr;

test::module::interface& test::module::interface::global() {
    return *g_test_module_intf_;
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);

    // Enable fail-fast
    GTEST_FLAG_SET(fail_fast, true);

    // initialize and manage hce framework memory
    auto lifecycle = hce::initialize();

    // initialize further RAII
    test::module::interface i;
    g_test_module_intf_ = &i;
    return RUN_ALL_TESTS();
}
