#include <string>
#include <algorithm>
#include "debug.h"
#include "gtest/gtest.h"
#include "dos/system.h"
#include "dos/util.h"
#include "dos/error.h"
#include "dos/mz.h"
#include "dos/analysis.h"

using namespace std;

// TODO: remove/rename this test, turn into library module test
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

TEST_F(SystemTest, RoutineMap) {
    // test loading of routine map from ida file
    const RoutineMap idaMap{"../bin/hello.lst"};
    idaMap.dump();
    // test comparing of routine maps for both the success and failure case
    RoutineMap matchMap{idaMap};
    ASSERT_TRUE(idaMap.match(matchMap));
    const Size routineCount = matchMap.routines.size();
    auto &r = matchMap.routines[routineCount / 2]; 
    r.extents.end = Address(0x1234, 0xabcd); // break end of one routine
    TRACELN("Altered end extent for routine " << r.toString());
    const Size matchCount = idaMap.match(matchMap);
    ASSERT_EQ(matchCount, routineCount - 1);
}

TEST_F(SystemTest, FindRoutines) {
    const RoutineMap idaMap{"../bin/hello.lst"};
    // test discovery of the routine map    
    MzImage mz{"bin/hello.exe"};
    mz.load(0x60);
    RoutineMap discoveredMap = findRoutines(Executable{mz});
    discoveredMap.dump();
    // compare against ida map
    const Size matchCount = idaMap.match(discoveredMap);
    TRACELN("Found matching " << matchCount << " routines out of " << idaMap.routines.size());
    ASSERT_GE(matchCount, 32); // not all 54 functions that ida finds can be identified for now
}

TEST_F(SystemTest, CodeCompare) {
    MzImage mz{"bin/hello.exe"};
    mz.load(0);
    Executable e1{mz}, e2{mz};
    ASSERT_TRUE(compareCode(e1, e2));
}

TEST_F(SystemTest, SignedHex) {
    SWord pos16val = 0x1234;
    ASSERT_EQ(signedHexVal(pos16val), "+0x1234");    
    SWord neg16val = -31989;
    ASSERT_EQ(signedHexVal(neg16val), "-0x7cf5");
    SByte pos8val = 0x10;
    ASSERT_EQ(signedHexVal(pos8val), "+0x10");
    SByte neg8val = -10;
    ASSERT_EQ(signedHexVal(neg8val), "-0x0a");
}