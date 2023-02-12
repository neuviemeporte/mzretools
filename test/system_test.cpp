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
        // if (HasFailure()) {
        //     TRACELN(sys.cpuInfo());
        // }
    }

    // access to private fields
    auto& getRoutines(RoutineMap &rm) { return rm.routines; }
    auto emptyRoutineMap() { return RoutineMap(); }
    auto emptySearchQueue() { return SearchQueue(); }
    auto& sqVisited(SearchQueue &sq) { return sq.visited; }
    auto& sqEntrypoints(SearchQueue &sq) { return sq.entrypoints; }
    void setEntrypoint(Executable &e, const Address &addr) { e.setEntrypoint(addr); }
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

TEST_F(SystemTest, RoutineMapFromQueue) {
    // test routine map generation from contents of a search queue
    SearchQueue sq = emptySearchQueue();
    vector<int> &visited = sqVisited(sq);
    vector<Address> &entrypoints = sqEntrypoints(sq);
    //          0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f  10 11 12 13 14 15 16 17
    visited = { 0, 0, 1, 1, 1, 0, 0, 0, 1, 1, 1, 1, 2, 2, 0, 0, 1, 1, 3, 0, 3, 3, 3, 0 };
    entrypoints = { 0x8, 0xc, 0x12 };
    RoutineMap queueMap{sq};
    queueMap.dump();
    ASSERT_EQ(queueMap.size(), 3);

    Routine r1 = queueMap.getRoutine(0);
    ASSERT_FALSE(r1.name.empty());
    ASSERT_EQ(r1.extents, Block(0x8, 0xb));
    ASSERT_EQ(r1.reachable.size(), 3);
    ASSERT_EQ(r1.reachable.at(0), Block(0x2, 0x4));
    ASSERT_EQ(r1.reachable.at(1), Block(0x8, 0xb));
    ASSERT_EQ(r1.reachable.at(2), Block(0x10, 0x11));
    ASSERT_EQ(r1.unreachable.size(), 1);
    ASSERT_EQ(r1.unreachable.at(0), Block(0x5, 0x7));

    Routine r2 = queueMap.getRoutine(1);
    ASSERT_FALSE(r2.name.empty());
    ASSERT_EQ(r2.extents, Block(0xc, 0xd));
    ASSERT_EQ(r2.reachable.size(), 1);
    ASSERT_EQ(r2.reachable.at(0), Block(0xc, 0xd));
    ASSERT_EQ(r2.unreachable.size(), 0);

    Routine r3 = queueMap.getRoutine(2);
    ASSERT_FALSE(r3.name.empty());
    ASSERT_EQ(r3.extents, Block(0x12, 0x16));
    ASSERT_EQ(r3.reachable.size(), 2);
    ASSERT_EQ(r3.reachable.at(0), Block(0x12, 0x12));
    ASSERT_EQ(r3.reachable.at(1), Block(0x14, 0x16));
    ASSERT_EQ(r3.unreachable.size(), 1);
    ASSERT_EQ(r3.unreachable.at(0), Block(0x13, 0x13));    
}

TEST_F(SystemTest, FindRoutines) {
    // discover routines inside an executable
    MzImage mz{"bin/hello.exe"};
    mz.load(0x0);
    Executable exe{mz};
    const RoutineMap &discoveredMap = exe.findRoutines();
    discoveredMap.dump();
    ASSERT_FALSE(discoveredMap.empty());

    // create map from IDA listing
    const RoutineMap idaMap{"../bin/hello.lst"};
    const Size idaMatchCount = 36; // not all 54 routines that ida finds can be identified for now    
    // compare our map against IDA map
    Size matchCount = idaMap.match(discoveredMap);
    TRACELN("Found matching " << matchCount << " routines out of " << idaMap.size());
    ASSERT_GE(matchCount, idaMatchCount);
    
    // save to file and reload
    discoveredMap.save("hello.map");
    RoutineMap reloadMap("hello.map");
    ASSERT_EQ(reloadMap.size(), discoveredMap.size());

    // check matching in the opposite direction, should be the same
    matchCount = reloadMap.match(idaMap);
    TRACELN("Found matching " << matchCount << " routines out of " << discoveredMap.size());
    ASSERT_GE(matchCount, idaMatchCount);
}

TEST_F(SystemTest, RoutineMapCollision) {
    const string path = "bad.map";
    RoutineMap rm = emptyRoutineMap();
    Block b1{100, 200}, b2{150, 250}, b3{300, 400};
    Routine r1{"r1", b1},  r2{"r2", b2}, r3{"r3", b3};
    auto &rv = getRoutines(rm);

    TRACELN("--- testing coliding routine extents");
    rv = { r1, r2 };
    rm.dump();
    rm.save(path);
    ASSERT_THROW(rm = RoutineMap{path}, AnalysisError);

    TRACELN("--- testing coliding routine extent with chunk");
    rv = { r1, r3 };
    rv.back().reachable.push_back(b2);
    rm.dump();
    rm.save(path);
    ASSERT_THROW(rm = RoutineMap{path}, AnalysisError);

    TRACELN("--- testing no colision");
    rv = { r1, r3 };
    rm.dump();
    rm.save(path);
    rm = RoutineMap{path};
    ASSERT_EQ(rm.size(), 2);
}

TEST_F(SystemTest, CodeCompare) {
    MzImage mz{"bin/hello.exe"};
    mz.load(0);
    Executable e1{mz}, e2{mz};
    auto map = RoutineMap{"hello.map"};

    // compare identical 
    ASSERT_TRUE(e1.compareCode(map, e2));

    // compare different
    setEntrypoint(e1, {0x115e}); // getnum
    setEntrypoint(e2, {0x8d0}); // _fflush
    ASSERT_FALSE(e1.compareCode(map, e2));
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