#include <gtest/gtest.h>

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);

    // Enable fail-fast
    GTEST_FLAG_SET(fail_fast, true);

    return RUN_ALL_TESTS();
}
