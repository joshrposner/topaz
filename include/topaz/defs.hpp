#pragma once
#include <cassert>

#define TPZ_VERSION_MAJOR 0
#define TPZ_VERSION_MINOR 1
#define TPZ_VERSION_PATCH 0

#if defined(_WIN32) || defined(_WIN64) || defined(__WIN32__) || \
    defined(__TOS_WIN__) || defined(__WINDOWS__)
#define TPZ_OS_WINDOWS
#elif defined(__linux__) || defined(__linux) || defined(linux) || \
    defined(__gnu_linux__)
#define TPZ_OS_LINUX
#elif defined(__APPLE__) || defined(__MACH__)
#define TPZ_OS_MAC
#else
#define TPZ_OS_UNKNOWN
#endif

#ifndef NDEBUG
#define TPZ_ASSERT(cond) assert(cond)
#else
#include <utility>
#define TPZ_ASSERT(cond)             \
  do {                               \
    if (!(cond)) std::unreachable(); \
  } while (0)
#endif
