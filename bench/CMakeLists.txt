include(FetchContent)

if(NOT CMAKE_BUILD_TYPE STREQUAL "Release" AND NOT CMAKE_CONFIGURATION_TYPES)
  message(WARNING "Benchmarks are being built in ${CMAKE_BUILD_TYPE} mode; results won't be meaningful.")
endif()

set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
# Benchmark builds itself with -Werror, which breaks as new Clang warnings appear
set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "" FORCE)

FetchContent_Declare(benchmark
  GIT_REPOSITORY https://github.com/google/benchmark.git
  GIT_TAG        v1.9.5
  GIT_SHALLOW    TRUE
  SYSTEM
  FIND_PACKAGE_ARGS NAMES benchmark
)
FetchContent_MakeAvailable(benchmark)

add_executable(tpz_bench
  smoke_bench.cpp
  # order_book_bench.cpp
)

target_link_libraries(tpz_bench
  PRIVATE
    topaz::topaz
    tpz_options
    benchmark::benchmark_main
)
tpz_copy_asan_runtime(tpz_bench)
