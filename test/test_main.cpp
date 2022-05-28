#include "gtest/gtest.h"
#include "debug.h"

DebugStream debug_stream;

int main(int argc, char* argv[]) {
    TRACE_ENABLE(true);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
