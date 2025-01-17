#include <gtest/gtest.h>

#include "hce.hpp"

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);

    // Enable fail-fast
    GTEST_FLAG_SET(fail_fast, true);

    // initialize and manage hce framework memory
    auto lifecycle = hce::initialize();

    return RUN_ALL_TESTS();
}
