cmake_minimum_required(VERSION 3.20)
project(BacktestEngine
    VERSION   1.0.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

if(NOT MSVC)
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -march=native")
endif()

option(BACKTEST_SANITIZE "Build with ASan + UBSan" OFF)
if(BACKTEST_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
    add_link_options(-fsanitize=address,undefined)
endif()

# ---------------------------------------------------------------------------
# Core library
# ---------------------------------------------------------------------------
add_library(backtest
    src/data_feed.cpp
    src/strategy.cpp
    src/broker.cpp
    src/portfolio.cpp
    src/engine.cpp
    src/metrics.cpp
)
target_include_directories(backtest PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# ---------------------------------------------------------------------------
# Examples
# ---------------------------------------------------------------------------
option(BACKTEST_BUILD_EXAMPLES "Build examples" ON)
if(BACKTEST_BUILD_EXAMPLES)
    add_executable(sma_crossover examples/sma_crossover.cpp)
    target_link_libraries(sma_crossover backtest)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
option(BACKTEST_BUILD_TESTS "Build unit tests" ON)
if(BACKTEST_BUILD_TESTS)
    enable_testing()
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.14.0
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    add_executable(test_engine tests/test_engine.cpp)
    target_link_libraries(test_engine backtest GTest::gtest_main)
    add_test(NAME EngineTests COMMAND test_engine)
endif()

# ---------------------------------------------------------------------------
# Benchmarks
# ---------------------------------------------------------------------------
option(BACKTEST_BUILD_BENCHMARKS "Build micro-benchmarks" ON)
if(BACKTEST_BUILD_BENCHMARKS)
    include(FetchContent)
    FetchContent_Declare(
        benchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG        v1.9.1
    )
    set(BENCHMARK_ENABLE_TESTING       OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_GTEST_TESTS   OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_WERROR        OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(benchmark)

    add_executable(bench_engine benchmarks/bench_engine.cpp)
    target_link_libraries(bench_engine backtest benchmark::benchmark)
endif()
