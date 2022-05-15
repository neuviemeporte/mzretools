#include <iostream>
#include <memory>
#include "memory.h"
#include "test/debug.h"
#include "gtest/gtest.h"

using namespace std;

TEST(Memory, Segmentation) {
    SegmentedAddress a(0x6ef, 0x1234);
    ASSERT_EQ(a.toLinear(), 0x8124);
    a.normalize();
    ASSERT_EQ(a.segment, 0x812);
    ASSERT_EQ(a.offset, 0x4);
    ASSERT_EQ(a.toLinear(), 0x8124);
}

TEST(Memory, Init) {
    auto mem = make_unique<Arena>();
    const Size memSize = mem->size();

    const Byte pattern[] = { 0xde, 0xad, 0xbe, 0xef };
    for (Offset i = 0; i < memSize; ++i) {
        ASSERT_EQ(mem->read(i), pattern[i % sizeof pattern]);
    }
}

TEST(Memory, Access) {
    auto mem = make_unique<Arena>();
    Byte* data = mem->base();
    Byte val = data[0];
}
