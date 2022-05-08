#include "dos.h"
#include "mz.h"
#include "test/debug.h"
#include "gtest/gtest.h"

using namespace std;

TEST(Dos, MzHeader) {
    MzImage mz("bin/hello.exe");
    TRACELN(mz.dump());
    ASSERT_EQ(mz.loadModuleSize(), 6723);
    ASSERT_EQ(mz.loadModuleOffset(), 512);
}