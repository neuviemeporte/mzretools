cmake_minimum_required(VERSION 3.5)

project(dostrace LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(libdos STATIC memory.cpp dos.cpp types.h error.h debug.h cpu.h memory.h dos.h)

add_executable(dostrace dostrace.cpp)
target_link_libraries(dostrace PUBLIC libdos)

add_subdirectory(googletest)
add_executable(test test.cpp test/cpu_test.cpp)
target_link_libraries(test PUBLIC libdos gtest)
target_include_directories(test PUBLIC googletest/googletest/include .)
