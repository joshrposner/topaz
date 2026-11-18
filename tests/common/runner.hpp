#pragma once

#include <filesystem>
#include <fstream>
#include <optional>
#include <vector>
#include <utility>
#include <topaz/order_book.hpp>
#include <topaz/defs.hpp>

namespace fs = std::filesystem;

using tpz::Event, tpz::Request;

namespace runner_detail {

typedef std::vector<std::pair<Request, std::optional<std::vector<Event>>>> Scenario;

/// Class to run a scenario test file
class Runner {

enum class Book {
  Empty,
  Spread,
  Deep,
};

};

inline Scenario parse_scenario(const fs::path& path) {
  Scenario s;
  
  std::ifstream file(path);
  TPZ_ASSERT(file.is_open());

  
}

} // namespace runner_detail
