#include <string>
#include <algorithm>
#include "debug.h"
#include "gtest/gtest.h"
#include "dos/util.h"
#include "dos/error.h"
#include "dos/mz.h"
#include "dos/analysis.h"
#include "dos/opcodes.h"
#include "dos/executable.h"

using namespace std;

class AnalysisTest : public ::testing::Test {
protected:

    // access to private fields
    auto& getRoutines(RoutineMap &rm) { return rm.routines; }
    auto emptyRoutineMap() { return RoutineMap(); }
    auto emptyScanQueue() { return ScanQueue(); }
    auto& sqVisited(ScanQueue &sq) { return sq.visited; }
    auto& sqEntrypoints(ScanQueue &sq) { return sq.entrypoints; }
};

// TODO: divest tests of analysis.cpp as distinct test suite
TEST_F(AnalysisTest, RegisterState) {
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

TEST_F(AnalysisTest, RoutineMap) {
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

TEST_F(AnalysisTest, RoutineMapFromQueue) {
    // test routine map generation from contents of a search queue
    ScanQueue sq = emptyScanQueue();
    vector<int> &visited = sqVisited(sq);
    vector<RoutineEntrypoint> &entrypoints = sqEntrypoints(sq);
    //          0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f  10 11 12 13 14 15 16 17
    visited = { 0, 0, 1, 1, 1, 0, 0, 0, 1, 1, 1, 1, 2, 2, 0, 0, 1, 1, 3, 0, 3, 3, 3, 0 };
    entrypoints = { {0x8, 1}, {0xc, 2}, {0x12, 3} };
    RoutineMap queueMap{sq, 0, visited.size()};
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

TEST_F(AnalysisTest, FindRoutines) {
    const Word loadSegment = 0x1234;
    // discover routines inside an executable
    MzImage mz{"bin/hello.exe"};
    mz.load(loadSegment);
    Executable exe{mz};
    const RoutineMap &discoveredMap = exe.findRoutines();
    TRACE(discoveredMap.dump());
    ASSERT_FALSE(discoveredMap.empty());

    // create map from IDA listing
    const RoutineMap idaMap{"../bin/hello.lst", loadSegment};
    const Size idaMatchCount = 37; // not all 54 routines that ida finds can be identified for now    
    // compare our map against IDA map
    Size matchCount = idaMap.match(discoveredMap);
    TRACELN("Discovered vs IDA, found matching " << matchCount << " routines out of " << idaMap.size());
    ASSERT_GE(matchCount, idaMatchCount);
    
    // save to file and reload
    discoveredMap.save("hello.map", loadSegment, true);
    RoutineMap reloadMap("hello.map", loadSegment);
    ASSERT_EQ(reloadMap.size(), discoveredMap.size());

    // check matching in the opposite direction, should be the same
    matchCount = reloadMap.match(idaMap);
    TRACELN("Reload vs IDA, found matching " << matchCount << " routines out of " << reloadMap.size());
    ASSERT_GE(matchCount, idaMatchCount);

    matchCount = discoveredMap.match(reloadMap);
    TRACELN("Discovered vs reload, found matching " << matchCount << " routines out of " << discoveredMap.size());
    ASSERT_EQ(matchCount, discoveredMap.size());
}

TEST_F(AnalysisTest, FindFarRoutines) {
    const Word loadSegment = 0x1000;
    // discover routines inside an executable
    MzImage mz{"bin/hellofar.exe"};
    mz.load(loadSegment);
    Executable exe{mz};
    const RoutineMap &discoveredMap = exe.findRoutines();
    TRACE(discoveredMap.dump());
    ASSERT_FALSE(discoveredMap.empty());
}

TEST_F(AnalysisTest, RoutineMapCollision) {
    const string path = "bad.map";
    RoutineMap rm = emptyRoutineMap();
    rm.setSegments({
        {"Code1", Segment::SEG_CODE, 0},
    });
    Block b1{100, 200}, b2{150, 250}, b3{300, 400};
    Routine r1{"r1", b1},  r2{"r2", b2}, r3{"r3", b3};
    auto &rv = getRoutines(rm);

    TRACELN("--- testing coliding routine extents");
    rv = { r1, r2 };
    TRACE(rm.dump());
    rm.save(path, 0, true);
    ASSERT_THROW(rm = RoutineMap{path}, ParseError);

    TRACELN("--- testing coliding routine extent with chunk");
    rv = { r1, r3 };
    rv.back().reachable.push_back(b2);
    TRACE(rm.dump());
    rm.save(path, 0, true);
    ASSERT_THROW(rm = RoutineMap{path}, ParseError);

    TRACELN("--- testing no colision");
    rv = { r1, r3 };
    TRACE(rm.dump());
    rm.save(path, 0, true);
    rm = RoutineMap{path};
    ASSERT_EQ(rm.size(), 2);
}

TEST_F(AnalysisTest, CodeCompare) {
    MzImage mz{"bin/hello.exe"};
    mz.load(0);
    Executable e1{mz}, e2{mz};
    auto map = RoutineMap{"hello.map"};

    // compare identical 
    ASSERT_TRUE(e1.compareCode(map, e2, {}));

    // compare different
    e1.setEntrypoint(Address(0x115e)); // getnum
    e2.setEntrypoint(Address(0x8d0)); // _fflush
    ASSERT_FALSE(e1.compareCode(map, e2, {}));
}

TEST_F(AnalysisTest, CodeCompareSkip) {
    // compare with skip
    const vector<Byte> refCode = {
        0x90, // nop
        0x07, // pop es
        0x0e, // push cs
        0x41, // inc cx
    };
    const vector<Byte> tgtCode = {
        0x58, // pop ax
        0x9c, // pushf
        0x41, // inc cx
    };

    Executable e1{0, refCode}, e2{0, tgtCode};
    AnalysisOptions opt;
    
    // test failure case
    opt.refSkip = 2;
    opt.tgtSkip = 2;
    ASSERT_FALSE(e1.compareCode(RoutineMap{}, e2, opt));

    // test success case
    opt.refSkip = 3;
    opt.tgtSkip = 2;
    ASSERT_TRUE(e1.compareCode(RoutineMap{}, e2, opt));
    
    const vector<Byte> ref2Code = {
        0x41, // inc cx
    };    
    const vector<Byte> tgt2Code = {
        0x41, // inc cx
    };
    Executable e3{0, ref2Code}, e4{0, tgt2Code};

    // test only ref skip
    opt.refSkip = 3;
    opt.tgtSkip = 0;
    ASSERT_TRUE(e1.compareCode(RoutineMap{}, e4, opt));

    // test only tgt skip
    opt.refSkip = 0;
    opt.tgtSkip = 2;
    ASSERT_TRUE(e3.compareCode(RoutineMap{}, e2, opt));
}

TEST_F(AnalysisTest, CodeCompareUnreachable) {
    // two blocks of identical code with an undefined opcode in the middle
    TRACELN("=== case 1");
    vector<Byte> 
        refCode = { OP_POP_ES, OP_PUSH_CS, OP_INC_CX, 0x60, OP_INC_AX, OP_PUSH_ES },
        tgtCode = refCode;
    Executable e1{0, refCode}, e2{0, tgtCode};
    AnalysisOptions opt;
    Routine r1{"test1", {0, refCode.size()}};
    // two reachable blocks separated by an unreachable one
    r1.reachable.push_back({0, 2});
    r1.unreachable.push_back({3, 3});
    r1.reachable.push_back({4, 5});
    // construct routine map for code
    RoutineMap map1;
    auto &rv1 = getRoutines(map1);
    rv1.push_back(r1);
    ASSERT_TRUE(e1.compareCode(map1, e2, opt));
    
    // different size of unreachable region, but make it possible to derive the offset mapping from a jump destination
    TRACELN("=== case 2");
    refCode = { OP_POP_ES, OP_PUSH_CS, OP_JMP_Jb, 0x1, 0x60, OP_INC_AX, OP_PUSH_ES };
    tgtCode = { OP_POP_ES, OP_PUSH_CS, OP_JMP_Jb, 0x3, 0x60, 0x61, 0x62, OP_INC_AX, OP_PUSH_ES };
    Executable e3{0, refCode}, e4 = Executable{0, tgtCode};
    Routine r2{"test2", {0, refCode.size()}};
    r2.reachable.push_back({0, 3});
    r2.unreachable.push_back({4, 4});
    r2.reachable.push_back({5, 6});
    // construct routine map for code
    RoutineMap map2;
    auto &rv2 = getRoutines(map2);
    rv2.push_back(r2);    
    ASSERT_TRUE(e3.compareCode(map2, e4, opt));

    // impossible to derive the offset mapping from a jump destination and instructions don't match
    TRACELN("=== case 3");
    refCode = { OP_POP_ES, OP_PUSH_CS, OP_NOP, 0x60, OP_INC_AX, OP_PUSH_ES };
    tgtCode = { OP_POP_ES, OP_PUSH_CS, OP_NOP, 0x60, OP_INC_CX, OP_PUSH_DS };
    Executable e5{0, refCode}, e6 = Executable{0, tgtCode};
    Routine r3{"test3", {0, refCode.size()}};
    r3.reachable.push_back({0, 2});
    r3.unreachable.push_back({3, 3});
    r3.reachable.push_back({4, 5});
    // construct routine map for code
    RoutineMap map3;
    auto &rv3 = getRoutines(map3);
    rv3.push_back(r3);    
    ASSERT_FALSE(e5.compareCode(map3, e6, opt));

    // TODO: implement test for different sized region and no jump after lookahead impemented, currently throws 
}

TEST_F(AnalysisTest, SignedHex) {
    SWord pos16val = 0x1234;
    ASSERT_EQ(signedHexVal(pos16val), "+0x1234");    
    SWord neg16val = -31989;
    ASSERT_EQ(signedHexVal(neg16val), "-0x7cf5");
    SByte pos8val = 0x10;
    ASSERT_EQ(signedHexVal(pos8val), "+0x10");
    SByte neg8val = -10;
    ASSERT_EQ(signedHexVal(neg8val), "-0x0a");
}

TEST_F(AnalysisTest, Variants) {
    ASSERT_EQ(splitString("a;bc;def", ';'), vector<string>({"a", "bc", "def"}));
    ASSERT_EQ(splitString("abcdef", ';'), vector<string>({"abcdef"}));
    stringstream str;
    str << "add sp, 0x2/pop cx/inc sp;inc sp" << endl 
        << "add sp, 0x4/pop cx;pop cx" << endl
        << "sub ax, ax/xor ax, ax";
    VariantMap vm{str};
    ASSERT_EQ(vm.maxDepth(), 2);
    vm.dump();
    VariantMap::MatchDepth m;
    m = vm.checkMatch({"sub ax, ax"}, {"xor ax, ax"});
    ASSERT_TRUE(m.isMatch());
    ASSERT_EQ(m.left, 1);
    ASSERT_EQ(m.right, 1);
    m = vm.checkMatch({"xor ax, ax"}, {"sub ax, ax"});
    ASSERT_TRUE(m.isMatch());
    ASSERT_EQ(m.left, 1);
    ASSERT_EQ(m.right, 1);
    m = vm.checkMatch({"add sp, 0x2"}, {"inc sp", "inc sp"});
    ASSERT_TRUE(m.isMatch());
    ASSERT_EQ(m.left, 1);
    ASSERT_EQ(m.right, 2);
    m = vm.checkMatch({"inc sp", "inc sp"}, {"add sp, 0x2"});
    ASSERT_TRUE(m.isMatch());
    ASSERT_EQ(m.left, 2);
    ASSERT_EQ(m.right, 1);
    m = vm.checkMatch({"foobar"}, {"inc sp", "inc sp"});
    ASSERT_FALSE(m.isMatch());
    m = vm.checkMatch({"inc sp", "inc sp"}, {"foobar"});
    ASSERT_FALSE(m.isMatch());
}