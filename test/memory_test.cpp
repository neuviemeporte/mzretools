#include <iostream>
#include "debug.h"
#include "gtest/gtest.h"
#include "dos/memory.h"
#include "dos/util.h"

using namespace std;

class MemoryTest : public ::testing::Test {
protected:
    Memory mem;
};

TEST_F(MemoryTest, Segmentation) {
    Address a(0x6ef, 0x1234);
    ASSERT_EQ(a.toLinear(), 0x8124);
    a.normalize();
    ASSERT_EQ(a.segment, 0x812);
    ASSERT_EQ(a.offset, 0x4);
    ASSERT_EQ(a.toLinear(), 0x8124);
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
}

TEST_F(MemoryTest, Block) {
    Block a{10, 20}, b{15,30}, c{30,40}, d{15,17}, e{20,50};
    ASSERT_TRUE(a.isValid());
    ASSERT_TRUE(b.isValid());
    ASSERT_TRUE(c.isValid());
    ASSERT_TRUE(d.isValid());
    // adjacent
    ASSERT_EQ(a.coalesce(e), Block(10,50));
    ASSERT_EQ(e.coalesce(a), Block(10,50));
    // partial
    ASSERT_EQ(a.coalesce(b), Block(10,30));
    ASSERT_EQ(b.coalesce(a), Block(10,30));
    // inclusion
    ASSERT_EQ(a.coalesce(d), Block(10,20));
    ASSERT_EQ(d.coalesce(a), Block(10,20));
    // disjoint
    ASSERT_FALSE(a.coalesce(c).isValid());
    ASSERT_FALSE(c.coalesce(a).isValid());
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