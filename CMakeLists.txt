cmake_minimum_required(VERSION 3.5)

project(dostrace LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ggdb -O0 -Wfatal-errors")

set(LIBDOS_SRC 
    src/registers.cpp
    src/cpu.cpp
    src/interrupt.cpp
    src/address.cpp
    src/memory.cpp
    src/segment.cpp
    src/psp.cpp
    src/analysis.cpp
    src/executable.cpp
    src/routine.cpp
    src/dos.cpp
    src/mz.cpp
    src/util.cpp
    src/opcodes.cpp
    src/output.cpp
    src/instruction.cpp
    src/modrm.cpp)

set(LIBDOS_HDR 
    include/dos/types.h
    include/dos/error.h
    include/dos/output.h
    include/dos/util.h
    include/dos/opcodes.h
    include/dos/registers.h
    include/dos/modrm.h
    include/dos/segment.h
    include/dos/analysis.h
    include/dos/executable.h
    include/dos/routine.h
    include/dos/cpu.h
    include/dos/interrupt.h
    include/dos/address.h
    include/dos/memory.h
    include/dos/psp.h
    include/dos/dos.h
    include/dos/mz.h
    include/dos/instruction.h)

# the DOS emulation library
add_library(libdos STATIC ${LIBDOS_SRC} ${LIBDOS_HDR})
target_include_directories(libdos PUBLIC include)

# Include Google testing framework
# Prevent overriding the parent project's compiler/linker settings on Windows
# Otherwise you get LNK2038
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
add_subdirectory(googletest)

set(TEST_SRC
    test/test_main.cpp
    test/debug.h
    test/cpu_test.cpp
    test/dos_test.cpp
    test/memory_test.cpp
    test/system_test.cpp)

# the test application executable
add_executable(runtest ${TEST_SRC})
target_include_directories(runtest PUBLIC include ${gtest_SOURCE_DIR}/include ${gmock_SOURCE_DIR}/include)
target_link_libraries(runtest PUBLIC gtest gmock libdos)
# run tests automatically as part of the build
add_custom_target(run_unit_test ALL COMMAND ./runtest DEPENDS runtest)

# utility executables
add_executable(mzmap tools/mzmap.cpp)
target_link_libraries(mzmap PUBLIC libdos)

add_executable(mzdiff tools/mzdiff.cpp)
target_link_libraries(mzdiff PUBLIC libdos)

add_executable(mzhdr tools/mzhdr.cpp)
target_link_libraries(mzhdr PUBLIC libdos)

add_executable(addrtool tools/addrtool.cpp)
target_link_libraries(addrtool PUBLIC libdos)

add_executable(psptool tools/psptool.cpp) 
target_link_libraries(psptool PUBLIC libdos)