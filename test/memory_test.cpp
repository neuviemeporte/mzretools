#include <iostream>
#include "memory.h"

#include "gtest/gtest.h"

TEST(Memory, Segmentation) {
    SegmentedAddress a(0x6ef, 0x1234);
    ASSERT_EQ(a.toLinear(), 0x8124);
    a.normalize();
    ASSERT_EQ(a.segment, 0x812);
    ASSERT_EQ(a.offset, 0x4);
    ASSERT_EQ(a.toLinear(), 0x8124);
}

TEST(Memory, Init) {
    Memory mem;
    const Size memSize = mem.size();

//    const Byte pattern[] = { 0xde, 0xad, 0xbe, 0xef };
//    for (Offset i = 0; i < memSize; ++i) {
//        ASSERT_EQ(mem.read(i), pattern[i % sizeof pattern]);
//    }
}
