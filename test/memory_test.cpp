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
    auto mem = make_unique<Memory>();
    const Size memSize = mem->size();

    const Byte pattern[] = { 0xde, 0xad, 0xbe, 0xef };
    for (Offset i = 0; i < memSize; ++i) {
        ASSERT_EQ(mem->read(i), pattern[i % sizeof pattern]);
        // all memory offsets should belong to the top-level mapping
        ASSERT_EQ(mem->mapLabel(i), "Memory");
    }
}

TEST(Memory, Mapping) {
    auto mem = make_unique<Memory>();
    MemoryRange range1(4, 7), range2(8, 9), range3(2, 8);
    std::string top("Memory"), label1("range1"), label2("range2"), label3("range3");
    const Size size = 12;
    // no mapping applied except the default top level
    for (Offset i = 0; i <= size; ++i) {
        const auto label = mem->mapLabel(i);
        TRACE(label << " ");
        ASSERT_EQ(label, top);
    }
    TRACELN("");
    // add single mapping
    mem->addMapping(range1, label1);
    for (Offset i = 0; i <= size; ++i) {
        const auto label = mem->mapLabel(i);
        TRACE(label << " ");
        if (range1.contains(i)) ASSERT_EQ(label, label1);
        else ASSERT_EQ(label, top);
    }
    TRACELN("");
    // add second, adjacent mapping
    mem->addMapping(range2, label2);
    for (Offset i = 0; i <= size; ++i) {
        const auto label = mem->mapLabel(i);
        TRACE(label << " ");
        if (range1.contains(i)) ASSERT_EQ(label, label1);
        else if (range2.contains(i)) ASSERT_EQ(label, label2);
        else ASSERT_EQ(label, top);
    }
    TRACELN("");
    // add overlapping mapping covering part of the top, entirety of first and part of second mapping
    mem->addMapping(range3, label3);
    for (Offset i = 0; i <= size; ++i) {
        const auto label = mem->mapLabel(i);
        TRACE(label << " ");
        if (i < range3.begin) ASSERT_EQ(label, top);
        else if (range3.contains(i)) ASSERT_EQ(label, label3);
        else if (range2.contains(i)) ASSERT_EQ(label, label2);
        else ASSERT_EQ(label, top);
    }
    TRACELN("");
}
