cmake_minimum_required(VERSION 3.5)

project(dostrace LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ggdb -Wfatal-errors")

set(LIBDOS_SRC 
    src/system.cpp
    src/registers.cpp
    src/cpu.cpp
    src/interrupt.cpp
    src/address.cpp
    src/memory.cpp
    src/psp.cpp
    src/dos.cpp
    src/mz.cpp
    src/util.cpp
    src/opcodes.cpp)

set(LIBDOS_HDR 
    include/dos/system.h
    include/dos/types.h
    include/dos/error.h
    include/dos/debug.h
    include/dos/util.h
    include/dos/opcodes.h
    include/dos/registers.h
    include/dos/modrm.h
    include/dos/cpu.h
    include/dos/interrupt.h
    include/dos/address.h
    include/dos/memory.h
    include/dos/psp.h
    include/dos/dos.h
    include/dos/mz.h)

# the DOS emulation library
add_library(libdos STATIC ${LIBDOS_SRC} ${LIBDOS_HDR})
target_include_directories(libdos PUBLIC include)

# the main application executable
add_executable(dostrace src/main.cpp)
target_include_directories(dostrace PUBLIC include)
target_link_libraries(dostrace PUBLIC libdos)

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
add_executable(mzhdr tools/mzhdr.cpp)
target_link_libraries(mzhdr PUBLIC libdos)

add_executable(addrtool tools/addrtool.cpp)
target_link_libraries(addrtool PUBLIC libdos)

add_executable(psptool tools/psptool.cpp) 
target_link_libraries(psptool PUBLIC libdos)