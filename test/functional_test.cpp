#include <string>

#include "system.h"
#include "util.h"
#include "test/debug.h"
#include "gtest/gtest.h"

using namespace std;

// TODO: make System support dep injection, use mock memory etc.

TEST(System, HelloWorld) {
    System sys;
    ASSERT_EQ(sys.command("load bin/hello.exe"), CMD_OK);
    ASSERT_EQ(sys.command("run"), CMD_OK);
}