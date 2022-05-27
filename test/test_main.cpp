#include "gtest/gtest.h"
#include "debug.h"

int main(int argc, char* argv[]) {
    TRACE_ENABLE(true);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
