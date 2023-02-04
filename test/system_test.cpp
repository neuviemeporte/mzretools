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

TEST_F(SystemTest, DISABLED_HelloWorld) {
    ASSERT_EQ(sys.command("load bin/hello.exe"), CMD_OK);
    EXPECT_THROW(sys.command("run"), CpuError);
    // TODO: reenable when complete instruction set implemented
    //ASSERT_EQ(sys.command("run"), CMD_OK);
}

// TODO: divest tests of analysis.cpp as distinct test suite
TEST_F(SystemTest, RegisterState) {
    RegisterState rs;

    TRACELN("Setting AX to 0x1234");
    rs.setValue(REG_AX, 0x1234);
    ASSERT_TRUE(rs.isKnown(REG_AX));
    ASSERT_TRUE(rs.isKnown(REG_AH));
    ASSERT_TRUE(rs.isKnown(REG_AL));
    ASSERT_EQ(rs.getValue(REG_AX), 0x1234);
    ASSERT_EQ(rs.getValue(REG_AH), 0x12);
    ASSERT_EQ(rs.getValue(REG_AL), 0x34);
    ASSERT_EQ(rs.regString(REG_AX), "AX = 1234"s);
    ASSERT_EQ(rs.regString(REG_AH), "AH = 12"s);
    ASSERT_EQ(rs.regString(REG_AL), "AL = 34"s);
    TRACELN(rs.toString());

    TRACELN("Marking AH as unknown");
    rs.setUnknown(REG_AH);
    ASSERT_FALSE(rs.isKnown(REG_AX));
    ASSERT_FALSE(rs.isKnown(REG_AH));
    ASSERT_TRUE(rs.isKnown(REG_AL));
    ASSERT_EQ(rs.getValue(REG_AX), 0);
    ASSERT_EQ(rs.getValue(REG_AH), 0);
    ASSERT_EQ(rs.getValue(REG_AL), 0x34);
    ASSERT_EQ(rs.regString(REG_AX), "AX = ??34"s);
    ASSERT_EQ(rs.regString(REG_AH), "AH = ??"s);
    ASSERT_EQ(rs.regString(REG_AL), "AL = 34"s);
    TRACELN(rs.toString());

    TRACELN("Setting BH to 0xab");
    rs.setValue(REG_BH, 0xab);
    ASSERT_FALSE(rs.isKnown(REG_BX));
    ASSERT_TRUE(rs.isKnown(REG_BH));
    ASSERT_FALSE(rs.isKnown(REG_BL));
    ASSERT_EQ(rs.getValue(REG_BX), 0);
    ASSERT_EQ(rs.getValue(REG_BH), 0xab);
    ASSERT_EQ(rs.getValue(REG_BL), 0);
    ASSERT_EQ(rs.regString(REG_BX), "BX = ab??"s);
    ASSERT_EQ(rs.regString(REG_BH), "BH = ab"s);
    ASSERT_EQ(rs.regString(REG_BL), "BL = ??"s);
    TRACELN(rs.toString());

    TRACELN("Marking BL as unknown");
    rs.setUnknown(REG_BL);
    ASSERT_EQ(rs.getValue(REG_BX), 0);
    ASSERT_EQ(rs.getValue(REG_BH), 0xab);
    ASSERT_EQ(rs.getValue(REG_BL), 0);
    ASSERT_EQ(rs.regString(REG_BX), "BX = ab??"s);
    ASSERT_EQ(rs.regString(REG_BH), "BH = ab"s);
    ASSERT_EQ(rs.regString(REG_BL), "BL = ??"s);    
    TRACELN(rs.toString());

    TRACELN("Setting BL to 0xcd");
    rs.setValue(REG_BL, 0xcd);
    ASSERT_EQ(rs.getValue(REG_BX), 0xabcd);
    ASSERT_EQ(rs.getValue(REG_BH), 0xab);
    ASSERT_EQ(rs.getValue(REG_BL), 0xcd);
    ASSERT_EQ(rs.regString(REG_BX), "BX = abcd"s);
    ASSERT_EQ(rs.regString(REG_BH), "BH = ab"s);
    ASSERT_EQ(rs.regString(REG_BL), "BL = cd"s);    
    TRACELN(rs.toString());    
}

TEST_F(SystemTest, RoutineMap) {
    // test loading of routine map from ida file
    const RoutineMap idaMap{"../bin/hello.lst"};
    idaMap.dump();
    // test comparing of routine maps for both the success and failure case
    RoutineMap matchMap{idaMap};
    ASSERT_TRUE(idaMap.match(matchMap));
    const Size routineCount = matchMap.size();
    const Size matchCount = idaMap.match(matchMap);
    ASSERT_EQ(matchCount, routineCount);
}

TEST_F(SystemTest, FindRoutines) {
    const RoutineMap idaMap{"../bin/hello.lst"};
    // test discovery of the routine map    
    MzImage mz{"bin/hello.exe"};
    mz.load(0x0);
    RoutineMap discoveredMap = findRoutines(Executable{mz});
    discoveredMap.dump();
    // compare against ida map
    const Size matchCount = idaMap.match(discoveredMap);
    TRACELN("Found matching " << matchCount << " routines out of " << idaMap.size());
    ASSERT_GE(matchCount, 34); // not all 54 functions that ida finds can be identified for now
}

TEST_F(SystemTest, DISABLED_CodeCompare) {
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