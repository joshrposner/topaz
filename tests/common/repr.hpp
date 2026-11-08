#pragma once

#include <sstream>
#include <string>

#include <topaz/order_book.hpp>
#include <utility>
#include <variant>
#include "topaz/types.hpp"

using tpz::VisitHelper;
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

namespace {

template <typename T>
std::string to_string_helper(const T& val) {
    std::ostringstream oss;
    oss << val;
    return oss.str();
}

template <typename First, typename... Rest>
std::string join(First first, Rest... rest) {
    std::string ret = to_string_helper(first);
    ((ret += ' ' + to_string_helper(rest)), ...);
    return ret;
}

} // namespace

inline std::string repr(Rejected::Reason r) {
  switch (r) {
    case Rejected::Reason::InvalidQty:
      return "INVALID QTY";
    case Rejected::Reason::OrderIdNotFound:
      return "NOT FOUND";
  }
}

inline std::string repr(Request r) {
  std::visit(VisitHelper(
    [](NewLimit nl) { return join(nl.side == Side::Ask ? "SELL" : "BUY", nl.qty.val(), '@', nl.price.val()); },
    [](NewMarket nm) { return join(nm.side == Side::Ask ? "SELL" : "BUY", nm.qty.val(), "MARKET"); },
    [](Cancel c) { return join("CANCEL", c.id.val()); }
  ), r);
  std::unreachable();
}

inline std::string repr(Event e) {
  std::visit(VisitHelper(
    [](Accepted a) { return join("ACCEPTED", a.id.val(), repr(a.request)); },
    [](Filled f) { return join("FILLED", f.id.val(), f.side == Side::Ask ? "ASK" : "BID", f.qty.val(), '@', f.price.val()); },
    [](Fulfilled ff) { return join("FULFILLED", ff.id.val()); },
    [](Canceled c) { return join("CANCELED", c.id.val()); },
    [](Rejected r) { return join("REJECTED", repr(r.request), repr(r.reason)); }
  ), e);
  std::unreachable();
}
