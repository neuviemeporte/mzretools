#include <string>

#include "dostrace.h"
#include "util.h"
#include "test/debug.h"
#include "gtest/gtest.h"

using namespace std;

// TODO: create fixture with paths, setup, teardown etc
TEST(Functional, LoadExe) {
    VM vm;
    auto loadOffset = vm.memory->freeStart();
    const auto loadPtr = vm.memory->pointer(loadOffset);
    ASSERT_EQ(execCommand(vm, "load ../bin/hello.exe"), CMD_OK);
    hexDump(loadPtr - 100, PAGE);
    auto dumpFile = checkFile("memory.bin");
    if (dumpFile.exists)
        ASSERT_TRUE(deleteFile("memory.bin"));
    ASSERT_EQ(execCommand(vm, "dump memory.bin"), CMD_OK);
    dumpFile = checkFile("memory.bin");
    ASSERT_TRUE(dumpFile.exists);
    ASSERT_EQ(dumpFile.size, vm.memory->size());
}
