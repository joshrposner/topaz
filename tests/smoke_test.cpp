#include <gtest/gtest.h>

#include <array>
#include <topaz/dsa/placeholder.hpp>
#include <topaz/engine/placeholder.hpp>
#include <topaz/net/placeholder.hpp>

using std::nullopt;

TEST(SmokeTest, BuildsAndRuns) { EXPECT_EQ(1 + 1, 2); }

TEST(SmokeTest, LinksAgainstTopaz) {
  EXPECT_EQ(tpz::dsa::placeholder(), 0);
  EXPECT_EQ(tpz::eng::placeholder(), 0);
  EXPECT_EQ(tpz::net::placeholder(), 0);
}
