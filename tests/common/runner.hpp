#pragma once

// Runs scenario files (tests/scenarios/**/*.txt). Format, one item per line:
//
//   @book empty|spread|deep       required; the sample book to start from
//   # comment                     must be on its own line
//   > REQUEST                     submit a request
//   | EVENT                       an expected event of the request above, in
//                                 order. A request with no `|` lines must emit
//                                 no events.
//   *                             don't check the request above's events
//   ? order ID SIDE QTY @ PRICE   order ID rests with this side, qty and price
//   ? no order ID                 order ID is not resting
//   ? orders N                    exactly N orders are resting
//   ? levels SIDE [PRICExQTY]...  levels(SIDE) is exactly this, best first
//   ? unchanged                   the book still matches the starting @book
//
// Requests: BUY|SELL QTY @ PRICE [GTC|IOC|FOK], BUY|SELL QTY MARKET [IOC|FOK],
// CANCEL ID. Events use repr() syntax. `?` checks run against the book as it
// is after every request above them.

#include <cstddef>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include <topaz/order_book.hpp>
#include <topaz/defs.hpp>

#include "parse.hpp"
#include "builders.hpp"
#include "print.hpp"  // GTest printers for Event lists, RestingOrder, Level
#include "repr.hpp"

namespace fs = std::filesystem;

using std::nullopt;
using tpz::Event, tpz::Request, tpz::OrderBook;
using parse_detail::Tokens;

using Events = std::vector<Event>;
using Levels = std::vector<Level>;

namespace runner_detail {

enum class Book {
  Empty,
  Spread,
  Deep,
};

struct ScenarioStep {
  Request req;
  /// nullopt means don't check (`*`)
  std::optional<Events> events = Events();
};

struct ExpectOrder { RestingOrder order; };
struct ExpectNoOrder { OrderId id; };
struct ExpectOrderCount { std::size_t n; };
struct ExpectLevels { Side side; Levels levels; };
struct ExpectUnchanged {};

using Check = std::variant<ExpectOrder, ExpectNoOrder, ExpectOrderCount,
                           ExpectLevels, ExpectUnchanged>;

struct ScenarioItem {
  std::size_t line_no;
  std::string line;
  std::variant<ScenarioStep, Check> item;
};

struct Scenario {
  Book book;
  std::vector<ScenarioItem> items;
};

inline Check parse_check(Tokens& t) {
  std::string_view kind = t.next();
  if (kind == "order") {
    OrderId id = t.next_int<OrderId>();
    Side side = parse_detail::parse_book_side(t);
    Qty qty = t.next_int<Qty>();
    t.expect("@");
    return ExpectOrder{RestingOrder(id, side, t.next_int<Price>(), qty)};
  }
  if (kind == "no") {
    t.expect("order");
    return ExpectNoOrder{t.next_int<OrderId>()};
  }
  if (kind == "orders") {
    auto n = t.next_int<Qty>().val();
    if (n < 0) t.fail("order count can't be negative");
    return ExpectOrderCount{static_cast<std::size_t>(n)};
  }
  if (kind == "levels") {
    ExpectLevels c{parse_detail::parse_book_side(t), {}};
    while (!t.peek().empty()) {
      std::string_view tok = t.next();
      std::size_t x = tok.find('x');
      if (x == std::string_view::npos) {
        t.fail(std::format("expected PRICExQTY, got \"{}\"", tok));
      }
      c.levels.push_back(Level(t.to_int<Price>(tok.substr(0, x)),
                               t.to_int<Qty>(tok.substr(x + 1))));
    }
    return c;
  }
  if (kind == "unchanged") return ExpectUnchanged{};
  t.fail(std::format("unknown check \"{}\"", kind));
}

inline void parse_line(std::string_view line, std::size_t line_no,
                       std::optional<Book>& book, Scenario& s) {
  Tokens t(line);
  std::string_view first = t.peek();
  if (first.empty() || first.starts_with('#')) return;
  t.next();

  if (first == "@book") {
    if (book) t.fail("@book given twice");
    std::string_view name = t.next();
    if (name == "empty") {
      book = Book::Empty;
    }
    else if (name == "spread") {
      book = Book::Spread;
    }
    else if (name == "deep") {
      book = Book::Deep;
    }
    else {
      t.fail(std::format("unknown book \"{}\"", name));
    }
  }
  else if (first == ">") {
    s.items.push_back({line_no, std::string(line),
                       ScenarioStep{parse_detail::parse_request(t)}});
  }
  else if (first == "|" || first == "*") {
    ScenarioStep* step = s.items.empty()
        ? nullptr : std::get_if<ScenarioStep>(&s.items.back().item);
    if (!step) t.fail(std::format("\"{}\" must follow a request", first));
    if (first == "*") {
      if (!step->events || !step->events->empty()) {
        t.fail("\"*\" can't be combined with expected events");
      }
      step->events = nullopt;
    }
    else {
      if (!step->events) t.fail("\"|\" can't follow \"*\"");
      step->events->push_back(parse_detail::parse_event(t));
    }
  }
  else if (first == "?") {
    s.items.push_back({line_no, std::string(line), parse_check(t)});
  }
  else {
    t.fail(std::format("unknown line type \"{}\"", first));
  }
  t.expect_end();
}

inline Scenario parse_scenario(const fs::path& path) {
  std::ifstream file(path);
  if (!file.is_open()) {
    throw std::runtime_error(std::format("can't open {}", path.string()));
  }

  Scenario s;
  std::optional<Book> book;
  std::string line;
  std::size_t line_no = 0;
  while (std::getline(file, line)) {
    ++line_no;
    try {
      parse_line(line, line_no, book, s);
    }
    catch (const std::invalid_argument& e) {
      throw std::invalid_argument(
          std::format("{}:{}: {}", path.filename().string(), line_no, e.what()));
    }
  }
  if (!book) {
    throw std::invalid_argument(
        std::format("{}: missing @book", path.filename().string()));
  }
  s.book = *book;
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

// spread: one order on each side, two ticks apart. The next order gets #3.
//
//   ask  101: #2 x10
//   bid   99: #1 x10
//
// deep: three levels per side, two orders at each best level (in arrival
// order). The next order gets #9.
//
//   ask  103: #8 x20
//   ask  102: #7 x10
//   ask  101: #5 x5, #6 x10
//   bid  100: #1 x5, #2 x10
//   bid   99: #3 x10
//   bid   98: #4 x20
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
  TPZ_UNREACHABLE();
  return book;
}

inline void run_check(const OrderBook& book, Book start, const Check& check) {
  std::visit(VisitHelper(
    [&](const ExpectOrder& c) {
      EXPECT_EQ(resting(book, c.order.id), c.order);
    },
    [&](const ExpectNoOrder& c) {
      EXPECT_EQ(resting(book, c.id), nullopt);
    },
    [&](const ExpectOrderCount& c) {
      EXPECT_EQ(book.orders().size(), c.n);
    },
    [&](const ExpectLevels& c) {
      EXPECT_EQ(book.levels(c.side), c.levels);
    },
    [&](const ExpectUnchanged&) {
      OrderBook fresh = make_book(start);
      EXPECT_EQ(book.levels(Side::Bid), fresh.levels(Side::Bid));
      EXPECT_EQ(book.levels(Side::Ask), fresh.levels(Side::Ask));
      EXPECT_TRUE(book.orders() == fresh.orders())
          << "resting orders differ from the starting book";
    }
  ), check);
}

inline void run(const fs::path& path) {
  Scenario s = parse_scenario(path);
  OrderBook book = make_book(s.book);

  for (const ScenarioItem& item : s.items) {
    SCOPED_TRACE(std::format("{}:{}: {}", path.filename().string(),
                             item.line_no, item.line));
    if (const auto* step = std::get_if<ScenarioStep>(&item.item)) {
      Events actual = submit(book, step->req);
      if (step->events) {
        EXPECT_EQ(actual, *step->events);
      }
    }
    else {
      run_check(book, s.book, std::get<Check>(item.item));
    }
  }
}

} // namespace runner_detail
