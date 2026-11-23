#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>

#include "common/runner.hpp"

class ScenarioTest : public ::testing::TestWithParam<fs::path> {};

TEST_P(ScenarioTest, Run) {
  runner_detail::run(GetParam());   // EXPECT_*s inside fire against this test
}

static std::vector<fs::path> collect_scenarios() {
  std::vector<fs::path> out;
  for (auto& e : fs::recursive_directory_iterator(TPZ_SCENARIO_DIR))
    if (e.path().extension() == ".txt") out.push_back(e.path());
  std::sort(out.begin(), out.end());   // stable ordering for ctest discovery
  return out;
}

INSTANTIATE_TEST_SUITE_P(
  Scenarios, ScenarioTest, ::testing::ValuesIn(collect_scenarios()),
  [](const auto& info) {
    // "id/first_order_gets_id_one.txt" -> "id_first_order_gets_id_one"
    auto rel = fs::relative(info.param, TPZ_SCENARIO_DIR).replace_extension();
    std::string name = rel.generic_string();
    std::replace(name.begin(), name.end(), '/', '_');
    return name;
  });
