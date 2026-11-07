#include <cstddef>
#include <string_view>

#include <topaz/engine/order_book.hpp>
#include "topaz/defs.hpp"

// SELL 5 @ 101
// SELL 5 @ 102
// BUY 8 @ 102
// ---
// ACCEPTED 1: SELL 5 @ 101
// ACCEPTED 2: SELL 5 @ 102
// ACCEPTED: (BUY 8 @ 102
// FILLED 3: BID 5 @ 101
// FILLED 1: ASK 5 @ 101
// FULFILLED 1
// ...

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
using tpz::eng::Accepted;
using tpz::eng::Canceled;
using tpz::eng::Event;
using tpz::eng::Filled;
using tpz::eng::Fulfilled;
using tpz::eng::Rejected;

tpz::Request parse_request(std::string_view line) {
  std::size_t delim = line.find(' ');
  TPZ_ASSERT(delim != std::string_view::npos);
  
}

tpz::eng::Event parse_event(std::string_view line) {

}