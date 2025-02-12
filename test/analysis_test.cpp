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
#include "dos/editdistance.h"

using namespace std;

class AnalysisTest : public ::testing::Test {
protected:
    // wrappers for access to private members, no this is not a black box test, why you ask?
    auto& getRoutines(CodeMap &rm) { return rm.routines; }
    void setMapSize(CodeMap &rm, const Size size) { rm.mapSize = size; }
    auto emptyCodeMap() { return CodeMap(); }
    auto emptyScanQueue() { return ScanQueue(); }
    auto& sqOrigin(ScanQueue &sq) { return sq.origin; }
    auto& sqVisited(ScanQueue &sq) { return sq.visited; }
    auto& sqEntrypoints(ScanQueue &sq) { return sq.entrypoints; }
    void mapSetSegments(CodeMap &rm, const vector<Segment> &segments) { rm.setSegments(segments); }
    const vector<Block>& getUnclaimed(const CodeMap &rm) { return rm.unclaimed; }
    auto analyzerInstructionMatch(Analyzer &a, const Executable &ref, const Executable &tgt, const Instruction &refInstr, const Instruction &tgtInstr) { 
        return a.instructionsMatch(ref, tgt, refInstr, tgtInstr); 
    }
    auto analyzerDiffVal() { return Analyzer::CMP_DIFFVAL; }
    auto analyzerDiffTgt() { return Analyzer::CMP_DIFFTGT; }
    bool crossCheck(const CodeMap &map1, const CodeMap &map2, const Size maxMiss) {
        TRACELN("Cross-checking map 1 (" + to_string(map1.routineCount()) + " routines) with map 2 (" + to_string(map2.routineCount()) + " routines)");
        Size missCount = 0;
        for (Size idx = 0; idx < map1.routineCount(); ++idx) {
            const Routine r1 = map1.getRoutine(idx);
            TRACELN("Routine " + to_string(idx + 1) + ": " + r1.dump(false));
            const Routine r2 = map2.getRoutine(r1.name);
            if (!r2.isValid()) {
                TRACELN("\tUnable to find in other map (" + to_string(++missCount) + ")");
                if (missCount > maxMiss) return false;
                continue;
            }
            TRACELN("\tFound equivalent: " + r2.dump(false));
            if (r1.size() != r2.size()) {
                TRACELN("\tRoutine sizes differ");
            }
        }
        return true;
    }
};

// TODO: divest tests of analysis.cpp as distinct test suite
TEST_F(AnalysisTest, CpuState) {
    CpuState rs;

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

TEST_F(AnalysisTest, CodeMap) {
    // test loading of routine map from ida file
    const CodeMap idaMap{"../bin/hello.lst", 0, CodeMap::MAP_IDALST};
    const auto sum = idaMap.getSummary();
    TRACE(sum.text);
    ASSERT_NE(sum.dataSize, 0);
    // test comparing of routine maps for both the success and failure case
    CodeMap matchMap{idaMap};
    const Size routineCount = matchMap.routineCount();
    const Size matchCount = idaMap.match(matchMap, false);
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

TEST_F(AnalysisTest, CodeMapFromQueue) {
    // test routine map generation from contents of a search queue
    ScanQueue sq = emptyScanQueue();
    vector<RoutineIdx> &visited = sqVisited(sq);
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
    CodeMap queueMap{sq, segments, {}, loadSegment, visited.size()};
    TRACE(queueMap.getSummary().text);
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

    const auto &unclaimed = getUnclaimed(queueMap);
    ASSERT_EQ(unclaimed.size(), 6);
    ASSERT_EQ(unclaimed.at(0), Block(0x0, 0x1));
    ASSERT_EQ(unclaimed.at(1), Block(0xe, 0xf));
    ASSERT_EQ(unclaimed.at(2), Block(0x10, 0x10));
    ASSERT_EQ(unclaimed.at(3), Block(0x17, 0x1c));
    ASSERT_EQ(unclaimed.at(4), Block(0x23, 0x2f));
    ASSERT_EQ(unclaimed.at(5), Block(0x30, 0x30 + 0xffff));

    TRACE(queueMap.getSummary().text);
}

TEST_F(AnalysisTest, BigCodeMap) {
    CodeMap rm{"../bin/egame.map", 0x1000};
    ASSERT_EQ(rm.routineCount(), 398);
}

TEST_F(AnalysisTest, FindRoutines) {
    const Word loadSegment = 0x1234;
    const Size expectedFound = 40;
    const Size expectedVars = 68;
    // discover routines inside an executable
    MzImage mz{"../bin/hello.exe"};
    mz.load(loadSegment);
    Executable exe{mz};
    Analyzer a{Analyzer::Options()};
    const CodeMap discoveredMap = a.exploreCode(exe);
    TRACE(discoveredMap.getSummary().text);
    ASSERT_FALSE(discoveredMap.empty());
    discoveredMap.save("hello.map", loadSegment, true);
    ASSERT_EQ(discoveredMap.routineCount(), expectedFound);
    ASSERT_EQ(discoveredMap.variableCount(), expectedVars);

    // create map from IDA listing
    const CodeMap idaMap{"../bin/hello.lst", loadSegment, CodeMap::MAP_IDALST};
    // compare our map against IDA map
    Size matchCount = idaMap.match(discoveredMap, true);
    const Size idaMatchCount = 38; // not all 54 routines that ida finds can be identified for now
    TRACELN("Discovered vs IDA, found matching " << matchCount << " routines out of " << idaMap.routineCount());
    ASSERT_EQ(matchCount, idaMatchCount);
    
    // reload from file
    CodeMap reloadMap("hello.map", loadSegment);
    ASSERT_EQ(reloadMap.routineCount(), discoveredMap.routineCount());
    TRACE(reloadMap.getSummary().text);

    // test the rebuilding of unclaimed blocks lost in a save to a file
    const auto &discoveredUnclaimed = getUnclaimed(discoveredMap);
    Size i = 0;
    TRACELN("Cross-checking unclaimed blocks between discovered and reloaded map");
    for (const auto &rub : getUnclaimed(reloadMap)) {
        const auto &dub = discoveredUnclaimed[i++];
        TRACELN(dub.toString() << " == " << rub.toString());
        // rub a dub
        ASSERT_EQ(dub, rub);
    }

    // check matching in the opposite direction, should be the same
    matchCount = reloadMap.match(idaMap, true);
    TRACELN("Reload vs IDA, found matching " << matchCount << " routines out of " << reloadMap.routineCount());
    ASSERT_EQ(matchCount, idaMatchCount);

    matchCount = discoveredMap.match(reloadMap, false);
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
    const CodeMap discoveredMap = a.exploreCode(exe);
    TRACE(discoveredMap.getSummary().text);
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
    const CodeMap discoveredMap = a.exploreCode(exe);
    TRACE(discoveredMap.getSummary().text);
    ASSERT_FALSE(discoveredMap.empty());
    ASSERT_GE(discoveredMap.routineCount(), 5);
    const auto segments = discoveredMap.getSegments();
    ASSERT_EQ(segments.size(), 1);
    const Segment s = segments.front();
    ASSERT_EQ(s.type, Segment::SEG_CODE);
}

TEST_F(AnalysisTest, CodeMapCollision) {
    const string path = "bad.map";
    CodeMap rm = emptyCodeMap();
    mapSetSegments(rm, {
        {"Code1", Segment::SEG_CODE, 0},
    });
    Block b1{100, 200}, b2{150, 250}, b3{300, 400};
    Routine r1{"r1", b1},  r2{"r2", b2}, r3{"r3", b3};
    auto &rv = getRoutines(rm);
    setMapSize(rm, b3.end.offset);

    TRACELN("--- testing coliding routine extents");
    rv = { r1, r2 };
    TRACE(rm.getSummary().text);
    rm.save(path, 0, true);
    ASSERT_THROW(rm = CodeMap{path}, ParseError);

    TRACELN("--- testing coliding routine extent with chunk");
    rv = { r1, r3 };
    rv.back().reachable.push_back(b2);
    TRACE(rm.getSummary().text);
    rm.save(path, 0, true);
    ASSERT_THROW(rm = CodeMap{path}, ParseError);

    TRACELN("--- testing no colision");
    rv = { r1, r3 };
    TRACE(rm.getSummary().text);
    rm.save(path, 0, true);
    rm = CodeMap{path};
    ASSERT_EQ(rm.routineCount(), 2);
}

TEST_F(AnalysisTest, OffsetMap) {
    OffsetMap om(2);
    ASSERT_TRUE(om.stackMatch(0xc, 0xa));
    ASSERT_TRUE(om.stackMatch(0xa, 0x2));
    ASSERT_TRUE(om.stackMatch(0xe, 0x6));
    ASSERT_TRUE(om.stackMatch(0x2, 0x8));
    ASSERT_TRUE(om.stackMatch(0x4, 0xc));
    ASSERT_TRUE(om.stackMatch(0x10, 0x4));
    ASSERT_TRUE(om.stackMatch(0x6, 0xe));
    // conflicts with mapping 4->c
    ASSERT_FALSE(om.stackMatch(0x3, 0xc));
    om.resetStack();
    ASSERT_TRUE(om.dataMatch(0x123, 0x456));
    // conflicts with previous but we have 2 data segments, still allowed
    ASSERT_TRUE(om.dataMatch(0x123, 0x567));
    // 3rd's the charm
    ASSERT_FALSE(om.dataMatch(0x123, 0x89a));
    // likewise the other way, 2nd ->567 allowed
    ASSERT_TRUE(om.dataMatch(0x456, 0x567));
    // 3rd mismatch fails
    ASSERT_FALSE(om.dataMatch(0x789, 0x567));
    ASSERT_TRUE(om.codeMatch({0x1000, 0xabc}, {0x1000, 0xcde}));
    ASSERT_FALSE(om.codeMatch({0x1000, 0xabc}, {0x1000, 0xdef}));
    ASSERT_FALSE(om.codeMatch({0x1000, 0x123}, {0x1000, 0xcde}));
}

TEST_F(AnalysisTest, CodeCompare) {
    const Word loadSegment = 0x1000;
    MzImage mz{"../bin/hello.exe"};
    mz.load(loadSegment);
    Executable e1{mz}, e2{mz};
    const string mapPath = "hello.map";
    auto map = CodeMap{mapPath, loadSegment};
    Analyzer::Options opt;
    opt.mapPath = mapPath;
    Analyzer a{opt};

    // compare identical 
    TRACELN("Test #1, comparison match");
    ASSERT_TRUE(a.compareCode(e1, e2, map));
    // cross-check target map
    CodeMap tgtMap("hello.tgt");
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
    opt.strict = false;
    Routine r1{"test1", {0, refCode.size()}};
    // two reachable blocks separated by an unreachable one
    r1.reachable.push_back({0, 2});
    r1.unreachable.push_back({3, 3});
    r1.reachable.push_back({4, 5});
    // construct routine map for code
    CodeMap map1;
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
    CodeMap map2;
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
    CodeMap map3;
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

TEST_F(AnalysisTest, DiffImmLow) {
    const vector<Byte> refCode = {
        0xC7,0x06,0x34,0x12,0xC3,0x00 // mov word [0x1234],0x13
    };
    const vector<Byte> objCode = {
        0xC7,0x06,0x34,0x12,0x12,0x00 // mov word [0x1234],0x12
    };
    Analyzer::Options opt;
    opt.strict = false;
    Executable e1{0, refCode}, e2{0, objCode};
    // decode instructions
    Instruction 
        i1{0, e1.codePointer(0)}, 
        i2{0, e2.codePointer(0)};
    Analyzer a(opt);
    ASSERT_EQ(analyzerInstructionMatch(a, e1, e2, i1, i2), analyzerDiffTgt());
}

TEST_F(AnalysisTest, FindDuplicates) {
    const Word loadSegment = 0x1234;
    const Size expectedRoutines = 40, expectedDuplicates = 23;
    MzImage mz{"../bin/hello.exe", loadSegment};
    Executable exe{mz};
    Analyzer::Options opt;
    opt.routineSizeThresh = 20;
    opt.routineDistanceThresh = 10;
    Analyzer a{opt};
    CodeMap rm = a.exploreCode(exe);
    TRACELN("Found routines: " + to_string(rm.routineCount()));
    for (int i = 0; i < rm.routineCount(); ++i) {
        ASSERT_FALSE(rm.getRoutine(i).duplicate);
    }
    ASSERT_EQ(rm.routineCount(), expectedRoutines);
    CodeMap rmdup{rm};
    ASSERT_EQ(rmdup.routineCount(), rm.routineCount());
    ASSERT_TRUE(a.findDuplicates(exe, exe, rm, rmdup));
    rmdup.save("hello.map.dup", loadSegment, true);
    Size foundDuplicates = 0;
    for (int i = 0; i < rmdup.routineCount(); ++i) {
        if (rmdup.getRoutine(i).duplicate) foundDuplicates++;
    }
    TRACELN("Found duplicates: " + to_string(foundDuplicates));
    ASSERT_EQ(foundDuplicates, expectedDuplicates);
}

TEST_F(AnalysisTest, EditDistance) {
    string s1 = "kitten", s2 = "sitting", s3 = "asdfvadfv";
    uint32_t maxDistance = numeric_limits<uint32_t>::max();
    ASSERT_EQ(edit_distance_dp_thr(s1.data(), s1.size(), s1.data(), s1.size(), 5), 0);
    ASSERT_EQ(edit_distance_dp_thr(s1.data(), s1.size(), s1.data(), s1.size(), 5), 0);
    ASSERT_EQ(edit_distance_dp_thr(s1.data(), s1.size(), s1.data(), s1.size(), 5), 0);
    ASSERT_EQ(edit_distance_dp_thr(s1.data(), s1.size(), s2.data(), s2.size(), 5), 3);
    ASSERT_EQ(edit_distance_dp_thr(s1.data(), s1.size(), s3.data(), s3.size(), 5), maxDistance);
    ASSERT_EQ(edit_distance_dp_thr(s1.data(), s1.size(), s3.data(), s3.size(), 5), maxDistance);
    ASSERT_EQ(edit_distance_dp_thr(s1.data(), s1.size(), s3.data(), s3.size(), 5), maxDistance);
}