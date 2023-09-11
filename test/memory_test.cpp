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
    Address a(0x6ef, 0x1234);
    ASSERT_EQ(a.toLinear(), 0x8124);
    a.normalize();
    ASSERT_EQ(a.segment, 0x812);
    ASSERT_EQ(a.offset, 0x4);
    ASSERT_EQ(a.toLinear(), 0x8124);
}

TEST_F(MemoryTest, Rebase) {
    Address src(0x1234, 0xa);
    src.rebase(0x1000);
    ASSERT_EQ(src.segment, 0);
    ASSERT_EQ(src.offset, 0x234a);
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