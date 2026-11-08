#pragma once

#include <string_view>
#include <ranges>

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

// SELL 5 @ 101
// SELL 5 @ 102
// BUY 8 @ 102
// ---
// ACCEPTED 1: SELL 5 @ 101
// ACCEPTED 2: SELL 5 @ 102
// ACCEPTED: BUY 8 @ 102
// FILLED 3: BID 5 @ 101
// FILLED 1: ASK 5 @ 101
// FULFILLED 1


// BUY 8 @ 102
// CANCEL 1
// CANCEL 1
// ---
// ACCEPTED 1: BUY 8 @ 102
// CANCELED 1
// REJECTED: CANCEL 1 (NOT FOUND)

// int main() {
//     std::string_view data = "C++;Rust;Go;Python";

//     // std::views::split yields sub-ranges of the original view lazily
//     for (auto&& subrange : data | std::views::split(';')) {
//         // Convert the subrange back to a string_view if needed
//         std::string_view token{subrange.begin(), subrange.end()};
//         std::cout << "Language: " << token << "\n";
//     }
// }

inline tpz::Request parse_request(std::string_view line) {

  for (auto&& subrange : line | std::views::split(' ')) {
    
  }
}

inline tpz::Event parse_event(std::string_view line) {

}
