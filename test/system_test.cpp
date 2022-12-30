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

TEST_F(SystemTest, Analysis) {
    MzImage mz{"bin/hello.exe"};
    mz.load(0);
    const auto &a = findRoutines(mz.loadModuleData(), mz.loadModuleSize(), mz.codeAddress());
    a.dump();
    const auto &routines = a.routines;
    // verify discovered extents for first routine
    auto firstExtents = Block{ 0x10, 0x1f };
    TRACELN("first routine: " << routines.front().toString());
    ASSERT_EQ(routines.front().extents, firstExtents);
    // find the 'start' routine, verify discovered extents
    auto startRoutineIt = std::find_if(routines.begin(), routines.end(), [](const Routine &r){
        return r.name == "start";
    });
    ASSERT_NE(startRoutineIt, routines.end());
    const auto &startRoutine = *startRoutineIt;
    TRACELN("start routine: " << startRoutine.toString());
    auto startExtents = Block{ 0x20, 0xc2 };
    ASSERT_EQ(startRoutine.extents, startExtents);
    // verify discovered extents for last routine
    auto lastExtents = Block{ 0x165e, 0x1680 };
    TRACELN("last routine: " << routines.back().toString());
    ASSERT_EQ(routines.back().extents, lastExtents);
}