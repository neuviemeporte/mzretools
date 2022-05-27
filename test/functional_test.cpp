#include <string>
#include "debug.h"
#include "gtest/gtest.h"
#include "dos/system.h"
#include "dos/util.h"

using namespace std;

// TODO: make System support dep injection, use mock memory etc.

TEST(System, HelloWorld) {
    System sys;
    ASSERT_EQ(sys.command("load bin/hello.exe"), CMD_OK);
    ASSERT_EQ(sys.command("run"), CMD_OK);
}