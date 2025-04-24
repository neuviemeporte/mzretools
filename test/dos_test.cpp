#include "debug.h"
#include "gtest/gtest.h"
#include "dos/dos.h"
#include "dos/mz.h"
#include "dos/util.h"

#include <vector>
#include <numeric>

using namespace std;

TEST(Dos, MzHeader) {
    MzImage mz("../bin/hello.exe");
    TRACELN(mz.dump());
    ASSERT_EQ(mz.loadModuleSize(), 6723);
    ASSERT_EQ(mz.loadModuleOffset(), 512);
}

TEST(Dos, HexDiff) {
    const Size bufSize = 0xf4;
    vector<Byte> buf1(bufSize);
    iota(buf1.begin(), buf1.end(), 1);
    vector<Byte> buf2(buf1);
    buf2[80] = 'z';
    hexDiff(buf1.data(), buf2.data(), 0x17, 0xe3, 0x1234, 0xabcd);
}