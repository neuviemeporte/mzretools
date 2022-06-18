#include <string>
#include "debug.h"
#include "gtest/gtest.h"
#include "dos/system.h"
#include "dos/util.h"
#include "dos/error.h"

using namespace std;

// TODO: make System support dep injection, use mock memory etc.
class SystemTest : public ::testing::Test {
protected:
    System sys;

protected:
    void TearDown() override {
        if (HasFailure()) {
            TRACELN(sys.cpuInfo());
        }
    }    
};

TEST_F(SystemTest, HelloWorld) {
    ASSERT_EQ(sys.command("load bin/hello.exe"), CMD_OK);
    EXPECT_THROW(sys.command("run"), CpuError);
    // TODO: reenable when complete instruction set implemented
    //ASSERT_EQ(sys.command("run"), CMD_OK);
}

TEST_F(SystemTest, Analysis) {
    ASSERT_EQ(sys.command("load bin/hello.exe"), CMD_OK);
    ASSERT_EQ(sys.command("analyze"), CMD_OK);
}