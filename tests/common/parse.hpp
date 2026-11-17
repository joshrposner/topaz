#pragma once

#include <charconv>
#include <cstddef>
#include <format>
#include <ranges>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <vector>

#include <topaz/order_book.hpp>

using tpz::Cancel;
using tpz::Level;
using tpz::MarketTif;
using tpz::NewLimit;
using tpz::NewMarket;
using tpz::OrderId;
using tpz::Price;
using tpz::Qty;
using tpz::Request;
using tpz::RestingOrder;
using tpz::Side;
using tpz::Tif;
using tpz::Accepted;
using tpz::Canceled;
using tpz::Event;
using tpz::Filled;
using tpz::Fulfilled;
using tpz::Rejected;

namespace parse_detail {

// Tokens are views into the line passed to the constructor, so it must outlive
// this object.
class Tokens {
 public:
  explicit Tokens(std::string_view line) : line_(line) {
    for (auto tok : line | std::views::split(' ')) {
      std::string_view s(tok.begin(), tok.end());
      if (!s.empty()) toks_.push_back(s);
    }
  }

  std::string_view next() {
    if (pos_ == toks_.size()) fail("unexpected end of line");
    return toks_[pos_++];
  }

  template <typename T>
  T next_int() {
    std::string_view tok = next();
    typename T::Rep val{};
    const char* first = tok.data();
    const char* last = tok.data() + tok.size();
    auto [ptr, ec] = std::from_chars(first, last, val);
    // from_chars parses "10x" as 10 without an error; ptr != last rejects it.
    if (ec != std::errc() || ptr != last) {
      fail(std::format("expected integer, got \"{}\"", tok));
    }
    return T(val);
  }

  void expect(std::string_view want) {
    std::string_view tok = next();
    if (tok != want) {
      fail(std::format("expected \"{}\", got \"{}\"", want, tok));
    }
  }

  void expect_end() const {
    if (pos_ != toks_.size()) {
      fail(std::format("unexpected trailing token \"{}\"", toks_[pos_]));
    }
  }

  [[noreturn]] void fail(std::string_view why) const {
    throw std::invalid_argument(
        std::format("parse error: {} in \"{}\"", why, line_));
  }

 private:
  std::string_view line_;
  std::vector<std::string_view> toks_;
  std::size_t pos_ = 0;
};

inline Side parse_book_side(Tokens& t) {
  std::string_view tok = t.next();
  if (tok == "BID") return Side::Bid;
  if (tok == "ASK") return Side::Ask;
  t.fail(std::format("expected BID or ASK, got \"{}\"", tok));
}

inline Request parse_request(Tokens& t) {
  std::string_view first = t.next();
  if (first == "CANCEL") return Cancel(t.next_int<OrderId>());

  Side side{};
  if (first == "BUY") {
    side = Side::Bid;
  } else if (first == "SELL") {
    side = Side::Ask;
  } else {
    t.fail(std::format("unknown request \"{}\"", first));
  }

  Qty qty = t.next_int<Qty>();
  std::string_view kind = t.next();
  if (kind == "MARKET") {
    return NewMarket(side, qty, MarketTif::ImmediateOrCancel);
  }
  if (kind == "@") {
    return NewLimit(side, t.next_int<Price>(), qty, Tif::GoodTilCancel);
  }
  t.fail(std::format("expected \"@\" or MARKET, got \"{}\"", kind));
}

inline Rejected::Reason parse_reason(Tokens& t) {
  std::string_view tok = t.next();
  if (tok == "INVALID") {
    t.expect("QTY");
    return Rejected::Reason::InvalidQty;
  }
  if (tok == "NOT") {
    t.expect("FOUND");
    return Rejected::Reason::OrderIdNotFound;
  }
  t.fail(std::format("unknown rejection reason starting \"{}\"", tok));
}

inline Event parse_event(Tokens& t) {
  std::string_view kind = t.next();
  if (kind == "ACCEPTED") {
    OrderId id = t.next_int<OrderId>();
    return Accepted(id, parse_request(t));
  }
  if (kind == "FILLED") {
    OrderId id = t.next_int<OrderId>();
    Side side = parse_book_side(t);
    Qty qty = t.next_int<Qty>();
    t.expect("@");
    return Filled(id, side, t.next_int<Price>(), qty);
  }
  if (kind == "FULFILLED") return Fulfilled(t.next_int<OrderId>());
  if (kind == "CANCELED") return Canceled(t.next_int<OrderId>());
  if (kind == "REJECTED") {
    Request request = parse_request(t);
    return Rejected(request, parse_reason(t));
  }
  t.fail(std::format("unknown event \"{}\"", kind));
}

}  // namespace parse_detail

inline Request parse_request(std::string_view line) {
  parse_detail::Tokens t(line);
  Request r = parse_detail::parse_request(t);
  t.expect_end();
  return r;
}

inline Event parse_event(std::string_view line) {
  parse_detail::Tokens t(line);
  Event e = parse_detail::parse_event(t);
  t.expect_end();
  return e;
}
