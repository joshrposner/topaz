#pragma once
#include <cassert>

#ifndef NDEBUG
#define TPZ_ASSERT(cond) assert(cond)
#else
#include <utility>
#define TPZ_ASSERT(cond)             \
  do {                               \
    if (!(cond)) std::unreachable(); \
  } while (0)
#endif
