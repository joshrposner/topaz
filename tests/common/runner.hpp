#pragma once

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include <topaz/order_book.hpp>
#include <topaz/defs.hpp>

#include "parse.hpp"
#include "builders.hpp"
#include "repr.hpp"

namespace fs = std::filesystem;

using Events = std::vector<Event>;

using std::nullopt;
using tpz::Event, tpz::Request, tpz::OrderBook;
using parse_detail::Tokens;

// make `Event` printable for GTest
namespace tpz {
inline void PrintTo(const Event& e, std::ostream* os) { *os << repr(e); }
}

namespace runner_detail {

enum class Book {
  Empty,
  Spread,
  Deep,
};

struct ScenarioStep {
  bool valid = false;
  Request req;
  std::optional<std::vector<Event>> events;
};

struct Scenario {
  Book book;
  std::vector<ScenarioStep> steps;
};

inline void parse_line(std::string_view line, Scenario& s, ScenarioStep& step) {
  if (line.empty()) return;
  Tokens t(line);
  std::string_view first = t.next();
  if (first == "#") {
    return;
  }
  else if (first == "@book") {
    std::string_view book = t.next();
    if (book == "empty") {
      s.book = Book::Empty;
    }
    else if (book == "spread") {
      s.book = Book::Spread;
    }
    else {
      s.book = Book::Deep;
    }
  }
  else if (first == ">") {
    if (step.valid) { // first close current step
      s.steps.push_back(step);
      step.req = Request();
      step.events.emplace();
    }
    step.valid = true;
    step.req = parse_detail::parse_request(t);
  }
  else if (first == "|") {
    if (!step.events.has_value()) step.events.emplace();
    step.events->push_back(parse_detail::parse_event(t));
  }
  else if (first == "*") {
    step.events = nullopt;
  }
}

inline Scenario parse_scenario(const fs::path& path) {
  Scenario s;
  ScenarioStep cur_step;
  
  std::ifstream file(path);
  TPZ_ASSERT(file.is_open());

  std::string line;
  while (std::getline(file, line)) {
    parse_line(line, s, cur_step);
  }
  if (cur_step.valid) s.steps.push_back(cur_step);

  return s;
}

inline Events submit(OrderBook& book, const Request& request) {
  Events out;
  book.submit(request, out);
  return out;
}

inline std::optional<RestingOrder> resting(const OrderBook& book, OrderId id) {
  auto it = book.orders().find(id);
  if (it == book.orders().end()) return std::nullopt;
  return it->second;
}

inline void rest(OrderBook& book, OrderId id, const Request& order) {
  EXPECT_EQ(submit(book, order), (Events{Accepted(id, order)}))
      << "while building a sample book";
}

inline OrderBook make_book(Book type) {
  OrderBook book;
  switch (type) {
    case Book::Empty:
      return book;
    case Book::Spread:
      rest(book, OrderId(1), bid(Price(99), Qty(10)));
      rest(book, OrderId(2), ask(Price(101), Qty(10)));
      return book;
    case Book::Deep:
      rest(book, OrderId(1), bid(Price(100), Qty(5)));
      rest(book, OrderId(2), bid(Price(100), Qty(10)));
      rest(book, OrderId(3), bid(Price(99), Qty(10)));
      rest(book, OrderId(4), bid(Price(98), Qty(20)));
      rest(book, OrderId(5), ask(Price(101), Qty(5)));
      rest(book, OrderId(6), ask(Price(101), Qty(10)));
      rest(book, OrderId(7), ask(Price(102), Qty(10)));
      rest(book, OrderId(8), ask(Price(103), Qty(20)));
      return book;
  }
}

inline void run(const fs::path& path) {
  Scenario s = parse_scenario(path);
  OrderBook book = make_book(s.book);

  for (std::size_t i = 0; i < s.steps.size(); i++) {
    const ScenarioStep& step = s.steps[i];
    SCOPED_TRACE(path.filename().string() + " step " + std::to_string(i + 1)
                 + ": " + repr(step.req));

    std::vector<Event> actual;
    book.submit(step.req, actual);
    if (step.events) {
      EXPECT_EQ(actual, *step.events);
    }
  }
}

} // namespace runner_detail
