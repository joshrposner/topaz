# ---------------------------------------------------------------------------
# Clean-slate build
# ---------------------------------------------------------------------------
# One-time setup. The default presets use Ninja + Clang on every OS, so the
# same commands work everywhere from any shell (no Developer prompt needed).
#
#   Fedora:  sudo dnf install git cmake ninja-build clang
#   Windows: Visual Studio 2022+ (or Build Tools) with "Desktop development
#            with C++", which Clang uses for the STL, CRT and Windows SDK.
#            Then: winget install Kitware.CMake Ninja-build.Ninja LLVM.LLVM
#            and make sure LLVM's bin/ is on PATH. On ARM64, check that
#            `clang++ --version` says aarch64; an x64 LLVM builds emulated
#            x64 binaries.
#
# Then, from a fresh clone:
#   cmake --workflow --preset test    | configure, build and test (Release)
#
# To start over, delete build/<preset>/ (or all of build/).
#
# ---------------------------------------------------------------------------
# CMakePresets (each builds in build/<preset>/)
# ---------------------------------------------------------------------------
#   release     Ninja + Clang, Release
#   debug-asan  Ninja + Clang, Debug + ASan (+UBSan, not on Windows)
#   msvc        Windows only: Visual Studio generator + MSVC, Release
#   msvc-asan   Windows only: Visual Studio generator + MSVC, Debug + ASan
#               (ARM64 needs VS 2026+ with the MSVC AddressSanitizer component)
#
# cmake --preset <name>             | configure
# cmake --build --preset <name>     | build
# ctest --preset <name>             | test
#
# Workflows (configure, build and test in one command):
# cmake --workflow --preset test             | release
# cmake --workflow --preset test-asan        | debug-asan
# cmake --workflow --preset test-msvc        | msvc
# cmake --workflow --preset test-msvc-asan   | msvc-asan
cmake_minimum_required(VERSION 3.25)

project(topaz
  VERSION 0.1.0
  DESCRIPTION "Low-latency limit order book and matching engine"
  LANGUAGES CXX
)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(TPZ_BUILD_EXAMPLES     "Build the Dear ImGui example app"      ${PROJECT_IS_TOP_LEVEL})
option(TPZ_BUILD_TESTS        "Build unit tests (GoogleTest)"         ${PROJECT_IS_TOP_LEVEL})
option(TPZ_BUILD_BENCH        "Build benchmarks (Google Benchmark)"   ${PROJECT_IS_TOP_LEVEL})
option(TPZ_ENABLE_SANITIZERS  "Enable ASan (+UBSan off Windows), Debug" OFF)
option(TPZ_WARNINGS_AS_ERRORS "Treat warnings as errors"              OFF)

# ---------------------------------------------------------------------------
# Global settings
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Clang's ASan on Windows doesn't support the debug CRT, so use the release
# CRT in Debug too. Set before any targets (including fetched dependencies)
# are created so everything links against the same runtime.
if(WIN32 AND NOT MSVC AND TPZ_ENABLE_SANITIZERS)
  set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
endif()

# ---------------------------------------------------------------------------
# tpz_options: warnings + sanitizers for first-party targets only
# ---------------------------------------------------------------------------
add_library(tpz_options INTERFACE)

if(MSVC)
  target_compile_options(tpz_options INTERFACE /W4 /permissive- /Zc:__cplusplus)
  if(TPZ_WARNINGS_AS_ERRORS)
    target_compile_options(tpz_options INTERFACE /WX)
  endif()

  # MSVC only has ASan (no UBSan). It is incompatible with /RTC runtime
  # checks, which CMake adds to Debug flags by default, and with incremental
  # linking. Third-party code is built without ASan, so the STL container
  # annotations (string, vector, optional, ...) are all disabled to avoid
  # LNK2038 annotate_* mismatches.
  if(TPZ_ENABLE_SANITIZERS)
    string(REPLACE "/RTC1" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    target_compile_options(tpz_options INTERFACE
      $<$<CONFIG:Debug>:/fsanitize=address>)
    target_compile_definitions(tpz_options INTERFACE
      $<$<CONFIG:Debug>:_DISABLE_STL_ANNOTATION>)
    target_link_options(tpz_options INTERFACE
      $<$<CONFIG:Debug>:/INCREMENTAL:NO>)
  endif()
else()
  target_compile_options(tpz_options INTERFACE
    -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
  if(TPZ_WARNINGS_AS_ERRORS)
    target_compile_options(tpz_options INTERFACE -Werror)
  endif()

  # On Windows, Clang targets the MSVC ABI and STL: ASan only, as with MSVC
  # (UBSan coverage comes from the Linux builds), and the same STL annotation
  # workaround for uninstrumented third-party code.
  if(TPZ_ENABLE_SANITIZERS AND WIN32)
    target_compile_options(tpz_options INTERFACE
      $<$<CONFIG:Debug>:-fsanitize=address -fno-omit-frame-pointer>)
    target_compile_definitions(tpz_options INTERFACE
      $<$<CONFIG:Debug>:_DISABLE_STL_ANNOTATION>)
    target_link_options(tpz_options INTERFACE
      $<$<CONFIG:Debug>:-fsanitize=address>)
  elseif(TPZ_ENABLE_SANITIZERS)
    target_compile_options(tpz_options INTERFACE
      $<$<CONFIG:Debug>:-fsanitize=address,undefined -fno-omit-frame-pointer>)
    target_link_options(tpz_options INTERFACE
      $<$<CONFIG:Debug>:-fsanitize=address,undefined>)
  endif()
endif()

# Windows ASan executables need the ASan runtime DLL, which isn't on PATH:
# MSVC's lives next to cl.exe (only on PATH in a Developer shell), Clang's in
# its resource dir. Copy it next to the executable so it runs from any shell
# (including gtest_discover_tests' post-build run).
# Call this before gtest_discover_tests so the copy happens first.
function(tpz_copy_asan_runtime target)
  if(NOT (WIN32 AND TPZ_ENABLE_SANITIZERS))
    return()
  endif()
  if(MSVC)
    cmake_path(GET CMAKE_CXX_COMPILER PARENT_PATH runtime_dir)
  else()
    execute_process(
      COMMAND ${CMAKE_CXX_COMPILER} -print-resource-dir
      OUTPUT_VARIABLE resource_dir
      OUTPUT_STRIP_TRAILING_WHITESPACE)
    file(TO_CMAKE_PATH "${resource_dir}/lib/windows" runtime_dir)
  endif()
  # The suffix depends on the architecture (x86_64, aarch64), so match any;
  # copying an extra architecture's DLL is harmless.
  file(GLOB dlls "${runtime_dir}/clang_rt.asan*dynamic*.dll")
  foreach(dll IN LISTS dlls)
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${dll}" "$<TARGET_FILE_DIR:${target}>"
      VERBATIM)
  endforeach()
endfunction()

# ---------------------------------------------------------------------------
# Library: topaz (tpz::eng, tpz::dsa, tpz::net)
# Sources and headers are listed explicitly (no globbing).
# ---------------------------------------------------------------------------
add_library(topaz)
add_library(topaz::topaz ALIAS topaz)

target_sources(topaz
  PRIVATE
    src/engine/placeholder.cpp
    src/engine/order_book.cpp
    src/dsa/placeholder.cpp
    src/net/placeholder.cpp
  PUBLIC
    FILE_SET HEADERS
    BASE_DIRS include
    FILES
      include/topaz/defs.hpp
      include/topaz/types.hpp
      include/topaz/engine/placeholder.hpp
      include/topaz/engine/event.hpp
      include/topaz/engine/order_book.hpp
      include/topaz/dsa/placeholder.hpp
      include/topaz/dsa/list.hpp
      include/topaz/net/placeholder.hpp
)

target_include_directories(topaz
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(topaz PRIVATE tpz_options)

# Platform networking libs (adjust to match Garnet-derived code)
if(WIN32)
  target_link_libraries(topaz PUBLIC ws2_32)
endif()

# ---------------------------------------------------------------------------
# Subprojects (skipped automatically until their CMakeLists.txt exists)
# ---------------------------------------------------------------------------
if(TPZ_BUILD_TESTS AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt)
  enable_testing()
  add_subdirectory(tests)
endif()

if(TPZ_BUILD_BENCH AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/bench/CMakeLists.txt)
  add_subdirectory(bench)
endif()

if(TPZ_BUILD_EXAMPLES AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/examples/gui/CMakeLists.txt)
  add_subdirectory(examples/gui)
endif()