#pragma once

#include <gtest/gtest.h>

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
using tpz::OrderBook;
using tpz::Rejected;

namespace {

inline Request bid(Price price, Qty qty) {
  return NewLimit(Side::Bid, price, qty, Tif::GoodTilCancel);
}

inline Request ask(Price price, Qty qty) {
  return NewLimit(Side::Ask, price, qty, Tif::GoodTilCancel);
}

inline Request bidm(Qty qty) {
  return NewMarket(Side::Bid, qty, MarketTif::ImmediateOrCancel);
}

inline Request askm(Qty qty) {
  return NewMarket(Side::Ask, qty, MarketTif::ImmediateOrCancel);
}

} // namespace
