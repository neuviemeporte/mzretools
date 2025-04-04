#include <iostream>
#include "debug.h"
#include "gtest/gtest.h"
#include "dos/memory.h"
#include "dos/util.h"
#include "dos/error.h"

using namespace std;

class MemoryTest : public ::testing::Test {
protected:
    Memory mem;
};

TEST_F(MemoryTest, AddressFromString) {
    Address addr1{"1234:abcd"};
    TRACELN("addr1 = "s + addr1.toString());
    ASSERT_EQ(addr1.segment, 0x1234);
    ASSERT_EQ(addr1.offset, 0xabcd);
    
    // normalization
    Address addr2{"0x1234", true};
    TRACELN("addr2 = "s + addr2.toString());
    ASSERT_EQ(addr2.toLinear(), 0x1234);
    ASSERT_EQ(addr2.segment, 0x123);
    ASSERT_EQ(addr2.offset, 0x4);
    
    Address addr3{"1234"};
    TRACELN("addr3 = "s + addr3.toString());
    ASSERT_EQ(addr3.toLinear(), 1234);

    // no normalization
    Address addr4{"0x5678", false};
    TRACELN("addr4 = "s + addr4.toString());
    ASSERT_EQ(addr4.toLinear(), 0x5678);
    ASSERT_EQ(addr4.segment, 0);
    ASSERT_EQ(addr4.offset, 0x5678);    
}

TEST_F(MemoryTest, Segmentation) {
    const Word segment = 0x86ef, offset = 0x1234;
    Address a(segment, offset);
    const Offset linear = static_cast<Offset>(segment) * PARAGRAPH_SIZE + offset;
    ASSERT_EQ(a.toLinear(), linear);
    a.normalize();
    const Word normSeg = linear / PARAGRAPH_SIZE, normOff = linear % PARAGRAPH_SIZE;
    TRACELN("normalized: " + a.toString());
    ASSERT_EQ(a.segment, normSeg);
    ASSERT_EQ(a.offset, normOff);
    ASSERT_FALSE(a.inSegment(0));
    const Word leftBoundSeg = OFFSET_TO_SEG(linear - 64_kB);
    TRACELN("left boundary segment: " + hexVal(leftBoundSeg));
    ASSERT_FALSE(a.inSegment(leftBoundSeg));
    // all segments from the boundary 64kb before and up to and including the normalized one can contain this address
    for (Word moveSeg = leftBoundSeg + 1; moveSeg <= normSeg; ++moveSeg) {
        ASSERT_TRUE(a.inSegment(moveSeg));
        a.move(moveSeg);
        TRACELN("moved to " + hexVal(moveSeg) + ": " + a.toString());
        ASSERT_EQ(a.segment, moveSeg);
        ASSERT_EQ(a.toLinear(), linear);
    }
    // segments just past the normalized one cannot contain this address
    ASSERT_FALSE(a.inSegment(normSeg + 1));
    ASSERT_THROW(a.move(normSeg + 1), MemoryError);
    ASSERT_FALSE(a.inSegment(normSeg + 2));
    ASSERT_THROW(a.move(normSeg + 2), MemoryError);
}

TEST_F(MemoryTest, Rebase) {
    Address src(0x1234, 0xa);
    src.rebase(0x1000);
    ASSERT_EQ(src.segment, 0x234);
    ASSERT_EQ(src.offset, 0xa);
}

TEST_F(MemoryTest, Move) {
    Address src(0x1234, 0xa);
    TRACELN("source: " + src.toString());
    
    const Word dest = 0x1000;
    Address a = src;
    a.move(dest);
    TRACELN("after move to " + hexVal(dest) + ": " + a.toString());
    ASSERT_EQ(a, src);
    ASSERT_EQ(a.segment, dest);
    ASSERT_EQ(a.offset, 0x234a);
}

TEST_F(MemoryTest, Advance) {
    Address a(0xabcd, 0x10);
    vector<SByte> disp8{ 10, 100, INT8_MAX, static_cast<SByte>(UINT16_MAX), -10, -100, INT8_MIN };
    vector<Address> result8{ {0xabcd, 0x1a}, {0xabcd, 0x74}, {0xabcd, 0x8f}, {0xabcd, 0x0f}, {0xabcd, 0x06}, {0xabcd, 0xffac}, {0xabcd, 0xff90} };
    size_t i = 0;
    TRACELN("--- 8bit displacement");
    for (SByte d : disp8) {
        Address b = a + d;
        TRACELN("Address " << a << " displaced by " << (int)d << " = " << b);
        ASSERT_EQ(b, result8[i++]);
    }
    i = 0;
    vector<SWord> disp16{ 10, 1000, INT16_MAX, static_cast<SWord>(UINT16_MAX), -10, -1000, INT16_MIN };
    vector<Address> result16{ {0xabcd, 0x1a}, {0xabcd, 0x3f8}, {0xabcd, 0x800f}, {0xabcd, 0x0f}, {0xabcd, 0x06}, {0xabcd, 0xfc28}, {0xabcd, 0x8010} };
    TRACELN("--- 16bit displacement");
    for (SWord d : disp16) {
        Address b = a + d;
        TRACELN("Address " << a << " displaced by " << d << " = " << b);
        ASSERT_EQ(b, result16[i++]);
    }

    // advance past current segment
    SWord amount = 0xdead;
    a = Address(0x1234, 0xabcd);
    Offset before = a.toLinear();
    TRACELN("Advancing " << a << " by " << hexVal(amount));
    a += amount;
    Offset after = a.toLinear();
    TRACELN("After advance: " << a);
    ASSERT_EQ(after, before + amount);

    // advance within current segment
    before = after;
    amount = 0xab;
    TRACELN("Advancing " << a << " by " << hexVal(amount));
    a += amount;
    after = a.toLinear();
    TRACELN("After advance: " << a);
    ASSERT_EQ(after, before + amount);    

    const SWord displacement = -0xa;
    a = Address(0x1234, 0xabcd);
    Address b(a, displacement);
    ASSERT_EQ(b.toLinear(), a.toLinear() - 0xa);
}

TEST_F(MemoryTest, Block) {
    const Block a{10, 20}, b{15,30}, c{30,40}, d{15,17}, e{20,50}, f{0x12, 0x12}, g{0x13, 0x13};
    ASSERT_TRUE(a.isValid());
    ASSERT_TRUE(b.isValid());
    ASSERT_TRUE(c.isValid());
    ASSERT_TRUE(d.isValid());
    // adjacent
    ASSERT_EQ(f.coalesce(g), Block(0x12,0x13));
    // intersect by 1
    ASSERT_EQ(a.coalesce(e), Block(10,50));
    ASSERT_EQ(e.coalesce(a), Block(10,50));
    // intersect by more than 1
    ASSERT_EQ(a.coalesce(b), Block(10,30));
    ASSERT_EQ(b.coalesce(a), Block(10,30));
    // inclusion
    ASSERT_EQ(a.coalesce(d), Block(10,20));
    ASSERT_EQ(d.coalesce(a), Block(10,20));
    // equality
    ASSERT_EQ(a.coalesce(a), a);
    ASSERT_EQ(f.coalesce(f), f);
    // disjoint
    ASSERT_EQ(a.coalesce(c), a);
    ASSERT_EQ(c.coalesce(a), c);

    // from string
    Block s1{"1234:100", "1234:200"};
    TRACELN("Block 1: " << s1);
    ASSERT_EQ(s1.begin.segment, 0x1234);
    ASSERT_EQ(s1.begin.offset, 0x100);
    ASSERT_EQ(s1.end.segment, 0x1234);
    ASSERT_EQ(s1.end.offset, 0x200);
    Block s2{"1234:100", "0x12540"};
    TRACELN("Block 2: " << s2);
    ASSERT_EQ(s1, s2);
    Block s3{"1234:100", "75072"};
    TRACELN("Block 3: " << s3);
    ASSERT_EQ(s1, s3);
    Block s4("0x12440", "+0x100");
    TRACELN("Block 4: " << s4);
    ASSERT_EQ(s1, s4);
    Block s5("0x12440", "+256");
    TRACELN("Block 5: " << s5);
    ASSERT_EQ(s1, s5);    
}

TEST_F(MemoryTest, BlockCut) {
    // disjoint
    Block b1{1, 4}, b2{6, 10};
    TRACELN("-- Splitting " + b1.toString() + " with " + b2.toString());
    auto split = b1.cut(b2);
    for (const auto &b : split) TRACELN("split: " + b.toString());
    ASSERT_EQ(split.size(), 1);
    ASSERT_EQ(split.front(), b1);
    // intersect
    b1 = {1, 6};
    b2 = {4, 10};
    TRACELN("-- Splitting " + b1.toString() + " with " + b2.toString());
    split = b1.cut(b2);
    for (const auto &b : split) TRACELN("split: " + b.toString());
    ASSERT_EQ(split.size(), 1);
    ASSERT_EQ(split.front(), Block(1, 3));
    // contain other
    b1 = {1, 8};
    b2 = {3, 6};
    TRACELN("-- Splitting " + b1.toString() + " with " + b2.toString());
    split = b1.cut(b2);
    for (const auto &b : split) TRACELN("split: " + b.toString());
    ASSERT_EQ(split.size(), 2);
    ASSERT_EQ(split.front(), Block(1, 2));
    ASSERT_EQ(split.back(), Block(7, 8));
    // we are contained
    b1 = {6, 8};
    b2 = {3, 10};
    TRACELN("-- Splitting " + b1.toString() + " with " + b2.toString());
    split = b1.cut(b2);
    for (const auto &b : split) TRACELN("split: " + b.toString());
    ASSERT_TRUE(split.empty());

    // we are contained
    b1 = {Address{0x1f88,0x58a}, Address{0x1f88,0x5db}};
    b2 = {Address{0x1f88,0x58a}, Address{0x1f88,0x5c0}};
    TRACELN("-- Splitting " + b1.toString() + " with " + b2.toString());
    split = b1.cut(b2);
    for (const auto &b : split) TRACELN("split: " + b.toString());
    ASSERT_EQ(split.size(), 1);
    ASSERT_EQ(split.front(), Block(Address{0x1f88,0x5c1}, Address{0x1f88, 0x5db}));
}

TEST_F(MemoryTest, BlockSplit) {
    Block b{Address{0x2274, 0x70}, Address{0x628b, 0xebe}};
    size_t splitCount = (b.size() / 0x10000) + 1;
    TRACELN("Splitting block: " + b.toString() + " / " + b.toString(true, true) + " of size " + sizeStr(b.size()));
    auto split = b.splitSegments();
    TRACELN("Split result:");
    Size splitSpan = 0;
    for (Block b : split) {
        TRACELN(b.toString() + " / " + b.toString(true, true));
        splitSpan += b.size();
    }
    ASSERT_EQ(split.size(), splitCount);
    ASSERT_EQ(splitSpan, b.size());
    ASSERT_EQ(split.front().begin, b.begin);
    ASSERT_EQ(split.back().end, b.end);

    b.end = b.begin;
    TRACELN("Splitting block: " + b.toString() + " / " + b.toString(true, true) + " of size " + sizeStr(b.size()));
    split = b.splitSegments();
    TRACELN("Split result:");
    splitSpan = 0;
    for (Block b : split) {
        TRACELN(b.toString() + " / " + b.toString(true, true));
        splitSpan += b.size();
    }
    ASSERT_EQ(split.size(), 1);
    ASSERT_EQ(splitSpan, b.size());
}

TEST_F(MemoryTest, Init) {
    const Size memSize = mem.size();
    const Byte pattern[] = { 0xde, 0xad, 0xbe, 0xef };
    for (Offset i = 0; i < memSize; ++i) {
        ASSERT_EQ(mem.readByte(i), pattern[i % sizeof pattern]);
    }
}

TEST_F(MemoryTest, Access) {
    const Offset off = 0x1234;
    const Byte b = 0xab;
    const Word w = 0x12fe;
    const Byte a[] = { 0xca, 0xfe, 0xba, 0xbe };
    mem.writeByte(off, b);
    ASSERT_EQ(mem.readByte(off), b);
    mem.writeWord(off, w);
    ASSERT_EQ(mem.readWord(off), w);
    mem.writeBuf(off, a, sizeof(a));
    for (size_t i = 0; i < sizeof(a); ++i) {
        ASSERT_EQ(mem.readByte(off + i), a[i]);
    }
}

TEST_F(MemoryTest, Alloc) {
    const Size avail = mem.availableBlock();
    mem.allocBlock(avail);
    TRACELN("Allocated max block of " << avail << " paragraphs, free mem at " << hexVal(mem.freeStart()));
    ASSERT_EQ(mem.availableBlock(), 0);
    mem.freeBlock(avail);
    ASSERT_EQ(mem.availableBlock(), avail);
}

TEST_F(MemoryTest, BlockIntersect) {
    Block a(100, 200);

    Block b(100, 200);
    ASSERT_TRUE(b.intersects(a));

    b = Block(125, 175);
    ASSERT_TRUE(b.intersects(a));

    b = Block(50, 150);
    ASSERT_TRUE(b.intersects(a));

    b = Block(150, 250);
    ASSERT_TRUE(b.intersects(a));

    b = Block(10, 75);
    ASSERT_FALSE(b.intersects(a));

    b = Block(210, 275);
    ASSERT_FALSE(b.intersects(a));
}

TEST_F(MemoryTest, FindPattern) {
    Block where{0, 9};
    const Byte data[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
    Memory mem;
    mem.writeBuf(0, data, sizeof(data));

    const ByteString pattern1 = { 3, -1, 5, 6, -1};
    TRACELN("Looking for pattern1: " + numericToHexa(pattern1));
    const auto addr1 = mem.find(pattern1, where);
    TRACELN("Found pattern1: " << addr1.toString());
    ASSERT_EQ(addr1.toLinear(), 2);

    const ByteString pattern2 = { 5, 1, -1, 3, 8};
    TRACELN("Looking for pattern2: " + numericToHexa(pattern2));
    const auto addr2 = mem.find(pattern2, where);
    TRACELN("Found pattern2: " << addr2.toString());
    ASSERT_FALSE(addr2.isValid());

    const ByteString pattern3 = { 1, 2, 3, 4, -1, 6, 7, -1, 9, 10 };
    TRACELN("Looking for pattern3: " + numericToHexa(pattern3));
    const auto addr3 = mem.find(pattern3, where);
    TRACELN("Found pattern3: " << addr3.toString());
    ASSERT_TRUE(addr3.isValid());
    ASSERT_EQ(addr3.toLinear(), 0);
}
