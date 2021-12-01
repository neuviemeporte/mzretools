cmake_minimum_required(VERSION 3.5)

project(dostrace LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(LIBDOS_SRC memory.cpp syscall.cpp mz.cpp util.cpp)
set(LIBDOS_HDR types.h error.h debug.h util.h cpu.h memory.h syscall.h mz.h)

# the DOS emulation library
add_library(libdos STATIC ${LIBDOS_SRC} ${LIBDOS_HDR})

# the main application executable
add_executable(dostrace dostrace.cpp)
target_link_libraries(dostrace PUBLIC libdos)

# Include Google testing framework
# Prevent overriding the parent project's compiler/linker settings on Windows
# Otherwise you get LNK2038
# set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
# add_subdirectory(googletest)

# set(TEST_SRC
#     test/debug.h
#     test/debug.cpp
#     test/cpu_test.cpp
#     test/memory_test.cpp
#     test/dos_test.cpp)

# the test application executable
# add_executable(test test.cpp ${TEST_SRC})
# target_include_directories(test PUBLIC ${gtest_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
# target_link_libraries(test PUBLIC gtest libdos)
