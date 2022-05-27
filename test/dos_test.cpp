#include "debug.h"
#include "gtest/gtest.h"
#include "dos/dos.h"
#include "dos/mz.h"

using namespace std;

TEST(Dos, MzHeader) {
    MzImage mz("bin/hello.exe");
    TRACELN(mz.dump());
    ASSERT_EQ(mz.loadModuleSize(), 6723);
    ASSERT_EQ(mz.loadModuleOffset(), 512);
}