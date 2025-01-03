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
    // wrappers for access to private members, no this is not a black box test, why you ask?
    auto& getRoutines(RoutineMap &rm) { return rm.routines; }
    void setMapSize(RoutineMap &rm, const Size size) { rm.mapSize = size; }
    auto emptyRoutineMap() { return RoutineMap(); }
    auto emptyScanQueue() { return ScanQueue(); }
    auto& sqOrigin(ScanQueue &sq) { return sq.origin; }
    auto& sqVisited(ScanQueue &sq) { return sq.visited; }
    auto& sqEntrypoints(ScanQueue &sq) { return sq.entrypoints; }
    void mapSetSegments(RoutineMap &rm, const vector<Segment> &segments) { rm.setSegments(segments); }
    vector<Block>& mapUnclaimed(RoutineMap &rm) { return rm.unclaimed; }
    auto analyzerInstructionMatch(Analyzer &a, const Executable &ref, const Executable &tgt, const Instruction &refInstr, const Instruction &tgtInstr) { 
        return a.instructionsMatch(ref, tgt, refInstr, tgtInstr); 
    }
    auto analyzerDiffVal() { return Analyzer::CMP_DIFFVAL; }
    bool crossCheck(const RoutineMap &map1, const RoutineMap &map2, const Size maxMiss) {
        TRACELN("Cross-checking map 1 (" + to_string(map1.routineCount()) + " routines) with map 2 (" + to_string(map2.routineCount()) + " routines)");
        Size missCount = 0;
        for (Size idx = 0; idx < map1.routineCount(); ++idx) {
            const Routine r1 = map1.getRoutine(idx);
            TRACELN("Routine " + to_string(idx + 1) + ": " + r1.toString(false));
            const Routine r2 = map2.getRoutine(r1.name);
            if (!r2.isValid()) {
                TRACELN("\tUnable to find in other map (" + to_string(++missCount) + ")");
                if (missCount > maxMiss) return false;
                continue;
            }
            TRACELN("\tFound equivalent: " + r2.toString(false));
            if (r1.size() != r2.size()) {
                TRACELN("\tRoutine sizes differ");
            }
        }
        return true;
    }
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
    const Size routineCount = matchMap.routineCount();
    const Size matchCount = idaMap.match(matchMap);
    ASSERT_EQ(matchCount, routineCount);

    const string findName{"main"};
    const Address findAddr{0, 0x10};
    const Routine r1 = idaMap.getRoutine(findName);
    ASSERT_TRUE(r1.isValid());
    ASSERT_EQ(r1.name, findName);
    ASSERT_EQ(r1.entrypoint(), findAddr);

    const Routine r2 = idaMap.getRoutine(findAddr);
    ASSERT_TRUE(r2.isValid());
    ASSERT_EQ(r1.name, r2.name);
    ASSERT_EQ(r1.entrypoint(), r2.entrypoint());
}

TEST_F(AnalysisTest, RoutineMapFromQueue) {
    // test routine map generation from contents of a search queue
    ScanQueue sq = emptyScanQueue();
    vector<RoutineId> &visited = sqVisited(sq);
    vector<RoutineEntrypoint> &entrypoints = sqEntrypoints(sq);
    const Word loadSegment = 0;
    vector<Segment> segments = {
        {"TestSeg1", Segment::SEG_CODE, loadSegment},
        {"TestSeg2", Segment::SEG_CODE, 0x1},
        {"TestSeg3", Segment::SEG_CODE, 0x2},
        {"TestSeg4", Segment::SEG_CODE, 0x3},
    };
    visited = { 
    //  0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f
        0, 0, 1, 1, 1, 0, 0, 0, 1, 1, 1, 1, 2, 2, 0, 0, // 0
        0, 1, 1, 3, 3, 3, 3, 0, 0, 0, 0, 0, 0, 2, 2, 2, // 1
        3, 3, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 2
        0, 0                                            // 3
    };
    // force the end of the routine map to overflow the segment 
    // where the last (unreachable) block starts
    visited.insert(visited.end(), 70000, 0);
    entrypoints = { {0x8, 1}, {0xc, 2}, {0x13, 3} };
    RoutineMap queueMap{sq, segments, loadSegment, visited.size()};
    queueMap.dump();
    ASSERT_EQ(queueMap.routineCount(), 3);

    Routine r1 = queueMap.getRoutine(0);
    ASSERT_FALSE(r1.name.empty());
    ASSERT_EQ(r1.extents, Block(0x8, 0xb));
    ASSERT_EQ(r1.reachable.size(), 3);
    ASSERT_EQ(r1.reachable.at(0), Block(0x2, 0x4));
    ASSERT_EQ(r1.reachable.at(1), Block(0x8, 0xb));
    ASSERT_EQ(r1.reachable.at(2), Block(0x11, 0x12));
    ASSERT_EQ(r1.unreachable.size(), 1);
    ASSERT_EQ(r1.unreachable.at(0), Block(0x5, 0x7));

    Routine r2 = queueMap.getRoutine(1);
    ASSERT_FALSE(r2.name.empty());
    ASSERT_EQ(r2.extents, Block(0xc, 0xd));
    ASSERT_EQ(r2.reachable.size(), 2);
    ASSERT_EQ(r2.reachable.at(0), Block(0xc, 0xd));
    ASSERT_EQ(r2.reachable.at(1), Block(0x1d, 0x1f));    
    ASSERT_EQ(r2.unreachable.size(), 0);

    Routine r3 = queueMap.getRoutine(2);
    ASSERT_FALSE(r3.name.empty());
    ASSERT_EQ(r3.extents, Block(0x13, 0x16));
    ASSERT_EQ(r3.reachable.size(), 2);
    ASSERT_EQ(r3.reachable.at(0), Block(0x13, 0x16));
    ASSERT_EQ(r3.reachable.at(1), Block(0x20, 0x22));
    ASSERT_EQ(r3.unreachable.size(), 0);

    const auto &unclaimed = mapUnclaimed(queueMap);
    ASSERT_EQ(unclaimed.size(), 6);
    ASSERT_EQ(unclaimed.at(0), Block(0x0, 0x1));
    ASSERT_EQ(unclaimed.at(1), Block(0xe, 0xf));
    ASSERT_EQ(unclaimed.at(2), Block(0x10, 0x10));
    ASSERT_EQ(unclaimed.at(3), Block(0x17, 0x1c));
    ASSERT_EQ(unclaimed.at(4), Block(0x23, 0x2f));
    ASSERT_EQ(unclaimed.at(5), Block(0x30, 0x30 + 0xffff));

    TRACELN(queueMap.dump());
}

TEST_F(AnalysisTest, FindRoutines) {
    const Word loadSegment = 0x1234;
    const Size expectedFound = 39;
    // discover routines inside an executable
    MzImage mz{"../bin/hello.exe"};
    mz.load(loadSegment);
    Executable exe{mz};
    Analyzer a{Analyzer::Options()};
    const RoutineMap discoveredMap = a.findRoutines(exe);
    TRACE(discoveredMap.dump());
    ASSERT_FALSE(discoveredMap.empty());
    discoveredMap.save("hello.map", loadSegment, true);    
    ASSERT_EQ(discoveredMap.routineCount(), expectedFound);

    // create map from IDA listing
    const RoutineMap idaMap{"../bin/hello.lst", loadSegment};
    // compare our map against IDA map
    Size matchCount = idaMap.match(discoveredMap);
    const Size idaMatchCount = 37; // not all 54 routines that ida finds can be identified for now    
    TRACELN("Discovered vs IDA, found matching " << matchCount << " routines out of " << idaMap.routineCount());
    ASSERT_EQ(matchCount, idaMatchCount);
    
    // reload from file
    RoutineMap reloadMap("hello.map", loadSegment);
    ASSERT_EQ(reloadMap.routineCount(), discoveredMap.routineCount());

    // check matching in the opposite direction, should be the same
    matchCount = reloadMap.match(idaMap);
    TRACELN("Reload vs IDA, found matching " << matchCount << " routines out of " << reloadMap.routineCount());
    ASSERT_EQ(matchCount, idaMatchCount);

    matchCount = discoveredMap.match(reloadMap);
    TRACELN("Discovered vs reload, found matching " << matchCount << " routines out of " << discoveredMap.routineCount());
    ASSERT_EQ(matchCount, discoveredMap.routineCount());
}

TEST_F(AnalysisTest, FindFarRoutines) {
    const Word loadSegment = 0x1000;
    // discover routines inside an executable
    MzImage mz{"../bin/hellofar.exe"};
    mz.load(loadSegment);
    Executable exe{mz};
    Analyzer a{Analyzer::Options()};
    const RoutineMap discoveredMap = a.findRoutines(exe);
    TRACE(discoveredMap.dump());
    ASSERT_FALSE(discoveredMap.empty());

    Address ep1{loadSegment, 0};
    Routine far1 = discoveredMap.findByEntrypoint(ep1);
    ASSERT_TRUE(far1.isValid());
    ASSERT_EQ(far1.entrypoint().segment, loadSegment);

    Address ep2{loadSegment+1, 4};
    Routine far2 = discoveredMap.findByEntrypoint(ep2);
    ASSERT_TRUE(far2.isValid());
    ASSERT_EQ(far2.entrypoint().segment, loadSegment+1);    
}

TEST_F(AnalysisTest, FindWithRollback) {
    // Sample of code where control goes into the weeds, 
    // data within code after a far call which is not supposed to return.
    // This is supposed to test the capability of rolling back after encountering
    // an instruction error while scanning an executable.
    const string path = "../bin/dn1.bin";
    const auto status = checkFile(path);
    ASSERT_TRUE(status.exists);
    vector<Byte> code(status.size, Byte{0});
    ASSERT_TRUE(readBinaryFile(path, code.data()));
    MzImage mz{code};
    Executable exe{mz};
    Analyzer a{Analyzer::Options()};
    const RoutineMap discoveredMap = a.findRoutines(exe);
    TRACE(discoveredMap.dump());
    ASSERT_FALSE(discoveredMap.empty());
    ASSERT_GE(discoveredMap.routineCount(), 5);
    const auto segments = discoveredMap.getSegments();
    ASSERT_EQ(segments.size(), 1);
    const Segment s = segments.front();
    ASSERT_EQ(s.type, Segment::SEG_CODE);
}

TEST_F(AnalysisTest, RoutineMapCollision) {
    const string path = "bad.map";
    RoutineMap rm = emptyRoutineMap();
    mapSetSegments(rm, {
        {"Code1", Segment::SEG_CODE, 0},
    });
    Block b1{100, 200}, b2{150, 250}, b3{300, 400};
    Routine r1{"r1", b1},  r2{"r2", b2}, r3{"r3", b3};
    auto &rv = getRoutines(rm);
    setMapSize(rm, b3.end.offset);

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
    ASSERT_EQ(rm.routineCount(), 2);
}

TEST_F(AnalysisTest, CodeCompare) {
    const Word loadSegment = 0x1000;
    MzImage mz{"../bin/hello.exe"};
    mz.load(loadSegment);
    Executable e1{mz}, e2{mz};
    const string mapPath = "hello.map";
    auto map = RoutineMap{mapPath, loadSegment};
    Analyzer::Options opt;
    opt.mapPath = mapPath;
    Analyzer a{opt};

    // compare identical 
    TRACELN("Test #1, comparison match");
    ASSERT_TRUE(a.compareCode(e1, e2, map));
    // cross-check target map
    RoutineMap tgtMap("hello.tgt");
    ASSERT_TRUE(crossCheck(map, tgtMap, 0));

    // compare different
    TRACELN("Test #2, comparison mismatch");
    e1.setEntrypoint(Address(0x115e)); // getnum
    e2.setEntrypoint(Address(0x8d0)); // _fflush
    ASSERT_FALSE(a.compareCode(e1, e2, map));
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
    Analyzer::Options opt;
    
    // test failure case
    opt.refSkip = 2;
    opt.tgtSkip = 2;
    Analyzer a1(opt);
    ASSERT_FALSE(a1.compareCode(e1, e2, {}));

    // test success case
    opt.refSkip = 3;
    opt.tgtSkip = 2;
    Analyzer a2(opt);
    ASSERT_TRUE(a2.compareCode(e1, e2, {}));
    
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
    Analyzer a3(opt);
    ASSERT_TRUE(a3.compareCode(e1, e4, {}));

    // test only tgt skip
    opt.refSkip = 0;
    opt.tgtSkip = 2;
    Analyzer a4(opt);
    ASSERT_TRUE(a4.compareCode(e3, e2, {}));
}

TEST_F(AnalysisTest, CodeCompareUnreachable) {
    // two blocks of identical code with an undefined opcode in the middle
    TRACELN("=== case 1");
    vector<Byte> 
        refCode = { OP_POP_ES, OP_PUSH_CS, OP_INC_CX, 0x60, OP_INC_AX, OP_PUSH_ES },
        tgtCode = refCode;
    Executable e1{0, refCode}, e2{0, tgtCode};
    Analyzer::Options opt;
    Routine r1{"test1", {0, refCode.size()}};
    // two reachable blocks separated by an unreachable one
    r1.reachable.push_back({0, 2});
    r1.unreachable.push_back({3, 3});
    r1.reachable.push_back({4, 5});
    // construct routine map for code
    RoutineMap map1;
    auto &rv1 = getRoutines(map1);
    rv1.push_back(r1);
    Analyzer a1(opt);
    ASSERT_TRUE(a1.compareCode(e1, e2, map1));
    
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
    ASSERT_TRUE(a1.compareCode(e3, e4, map2));

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
    ASSERT_FALSE(a1.compareCode(e5, e6, map3));

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

TEST_F(AnalysisTest, DiffRegOffset) {
    const vector<Byte> refCode = {
        0xC7, 0x47, 0x06, 0x00, 0x00 // mov word [bx+0x6],0x0
    };
    const vector<Byte> objCode = {
        0xC7, 0x47, 0x0C, 0x00, 0x00 //  mov word [bx+0xc],0x0
    };
    Analyzer::Options opt;
    opt.strict = false;
    Executable e1{0, refCode}, e2{0, objCode};
    // decode instructions
    Instruction 
        i1{0, e1.codePointer(0)}, 
        i2{0, e2.codePointer(0)};
    Analyzer a(opt);
    ASSERT_EQ(analyzerInstructionMatch(a, e1, e2, i1, i2), analyzerDiffVal());
}

TEST_F(AnalysisTest, DiffMemAndImm) {
    const vector<Byte> refCode = {
        0xC7,0x06,0x02,0x72,0xCA,0x72 // mov word [0x7202],0x72ca
    };
    const vector<Byte> objCode = {
        0xC7,0x06,0x4A,0x72,0x12,0x73 // mov word [0x724a],0x7312
    };
    Analyzer::Options opt;
    opt.strict = false;
    Executable e1{0, refCode}, e2{0, objCode};
    // decode instructions
    Instruction 
        i1{0, e1.codePointer(0)}, 
        i2{0, e2.codePointer(0)};
    Analyzer a(opt);
    ASSERT_EQ(analyzerInstructionMatch(a, e1, e2, i1, i2), analyzerDiffVal());
}
