cmake_minimum_required(VERSION 3.20)
project(FastMarketParser CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Performance-critical compiler flags
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -mtune=native -flto -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -fno-exceptions -fno-rtti")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -funroll-loops -finline-functions")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -ffast-math -ftree-vectorize")

# Debug flags
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g -Wall -Wextra -Wpedantic -fsanitize=address,undefined")

# Find threads
find_package(Threads REQUIRED)

# Include directories
include_directories(include)

# Core library
add_library(market_parser STATIC
    src/itch_parser.cpp
    src/async_logger.cpp
    src/mpmc_queue.cpp
    src/system_utils.cpp
)

target_link_libraries(market_parser PRIVATE Threads::Threads)

# Benchmark executable
add_executable(parser_benchmark
    src/benchmark.cpp
)

target_link_libraries(parser_benchmark PRIVATE market_parser Threads::Threads)

# Test executable
add_executable(parser_test
    test/test_parser.cpp
)

target_link_libraries(parser_test PRIVATE market_parser Threads::Threads)

# Demo executable
add_executable(parser_demo
    src/demo.cpp
)

target_link_libraries(parser_demo PRIVATE market_parser Threads::Threads)
