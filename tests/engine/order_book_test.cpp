#include <gtest/gtest.h>

#include <topaz/engine/order_book.hpp>

#include <optional>
#include <ostream>
#include <utility>
#include <variant>
#include <vector>

// Contract these tests assume:
//   - Each book assigns ids to the new orders it accepts, starting at 1 and counting up in
//     submission order. Rejected orders and cancels don't use an id.
//   - A new order emits Accepted{id, request} first. One that can't be processed emits only
//     Rejected{request, reason}.
//   - Each match emits Filled for the incoming order, then Filled for the resting order. Both are
//     at the resting order's price, and each carries that order's own id and side.
//   - Right after a match's two Filled events, each order whose remaining qty reached zero gets
//     Fulfilled: the incoming order first, then the resting one.
//   - The unfilled remainder of a GTC limit order rests without emitting anything else.
//   - Market orders match at any price, best price first. IOC and FOK orders, limit or market,
//     never rest.
//   - An IOC order ends with Fulfilled if it filled completely. Otherwise its last event is
//     Cancelled{id} for the remainder, including when nothing filled.
//   - An FOK order either fills completely, with the same events as any other full fill, or emits
//     only Accepted, Cancelled{id} and leaves the book untouched. Only resting quantity within its
//     limit price counts toward filling it.
//   - A new order with zero qty emits only Rejected{request, ZeroQty}.
//   - Cancel emits only Cancelled{id}. Cancelling an id that isn't resting (unknown, already
//     filled, or already cancelled) emits only Rejected{cancel, OrderIdNotFound}.
//   - orders() holds exactly the resting orders, each with its remaining qty.
//
// Not covered yet: negative qty.

using tpz::Cancel;
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
using tpz::eng::Cancelled;
using tpz::eng::Event;
using tpz::eng::Filled;
using tpz::eng::Fulfilled;
using tpz::eng::OrderBook;
using tpz::eng::Rejected;

using Events = std::vector<Event>;

// Printers so failures show readable values instead of raw bytes. They live in the types' own
// namespaces so GoogleTest finds them by argument-dependent lookup.
namespace tpz {

template<typename Tag>
std::ostream& operator<<(std::ostream& os, StrongInt<Tag> v) { return os << v.val(); }

std::ostream& operator<<(std::ostream& os, Side side) {
  return os << (side == Side::Bid ? "Bid" : "Ask");
}

std::ostream& operator<<(std::ostream& os, Tif tif) {
  switch (tif) {
    case Tif::GoodTilCancel:     return os << "GTC";
    case Tif::ImmediateOrCancel: return os << "IOC";
    case Tif::FillOrKill:        return os << "FOK";
  }
  return os << "Tif(" << std::to_underlying(tif) << ')';
}

std::ostream& operator<<(std::ostream& os, MarketTif tif) {
  switch (tif) {
    case MarketTif::ImmediateOrCancel: return os << "IOC";
    case MarketTif::FillOrKill:        return os << "FOK";
  }
  return os << "MarketTif(" << std::to_underlying(tif) << ')';
}

std::ostream& operator<<(std::ostream& os, const NewLimit& o) {
  return os << "NewLimit{" << o.side << ' ' << o.qty << " @ " << o.price << ' ' << o.tif << '}';
}

std::ostream& operator<<(std::ostream& os, const NewMarket& o) {
  return os << "NewMarket{" << o.side << ' ' << o.qty << ' ' << o.tif << '}';
}

std::ostream& operator<<(std::ostream& os, const Cancel& c) {
  return os << "Cancel{#" << c.id << '}';
}

std::ostream& operator<<(std::ostream& os, const Request& r) {
  std::visit([&](const auto& alt) { os << alt; }, r);
  return os;
}

std::ostream& operator<<(std::ostream& os, const RestingOrder& o) {
  return os << "RestingOrder{#" << o.id << ' ' << o.side << ' ' << o.qty << " @ " << o.price << '}';
}

} // namespace tpz

namespace tpz::eng {

std::ostream& operator<<(std::ostream& os, const Accepted& e) {
  return os << "Accepted{#" << e.id << ' ' << e.request << '}';
}

std::ostream& operator<<(std::ostream& os, const Filled& e) {
  return os << "Filled{#" << e.id << ' ' << e.side << ' ' << e.qty << " @ " << e.price << '}';
}

std::ostream& operator<<(std::ostream& os, const Fulfilled& e) {
  return os << "Fulfilled{#" << e.id << '}';
}

std::ostream& operator<<(std::ostream& os, const Cancelled& e) {
  return os << "Cancelled{#" << e.id << '}';
}

std::ostream& operator<<(std::ostream& os, Rejected::Reason reason) {
  switch (reason) {
    case Rejected::Reason::ZeroQty:         return os << "ZeroQty";
    case Rejected::Reason::OrderIdNotFound: return os << "OrderIdNotFound";
  }
  return os << "Reason(" << std::to_underlying(reason) << ')';
}

std::ostream& operator<<(std::ostream& os, const Rejected& e) {
  return os << "Rejected{" << e.request << ", " << e.reason << '}';
}

} // namespace tpz::eng

namespace {

Request bid(Price price, Qty qty) {
  return NewLimit{Side::Bid, price, qty, Tif::GoodTilCancel};
}

Request ask(Price price, Qty qty) {
  return NewLimit{Side::Ask, price, qty, Tif::GoodTilCancel};
}

Events submit(OrderBook& book, const Request& request) {
  Events out;
  book.submit(request, out);
  return out;
}

std::optional<RestingOrder> resting(const OrderBook& book, OrderId id) {
  auto it = book.orders().find(id);
  if (it == book.orders().end()) return std::nullopt;
  return it->second;
}

// Submits an order that should be assigned `id` and rest without matching. Sample books are built
// through the public API, so a bug could build the wrong book; this reports that at the source
// instead of letting later assertions fail in confusing ways.
void rest(OrderBook& book, OrderId id, const Request& order) {
  EXPECT_EQ(submit(book, order), (Events{Accepted{id, order}})) << "while building a sample book";
}

// One order on each side, two ticks apart. The next order submitted gets #3.
//
//   ask  101: #2 x10
//   bid   99: #1 x10
OrderBook spread_book() {
  OrderBook book;
  rest(book, OrderId{1}, bid(Price{99}, Qty{10}));
  rest(book, OrderId{2}, ask(Price{101}, Qty{10}));
  return book;
}

// Three levels per side, with two orders at each best level (listed in arrival order). The next
// order submitted gets #9.
//
//   ask  103: #8 x20
//   ask  102: #7 x10
//   ask  101: #5 x5, #6 x10
//   bid  100: #1 x5, #2 x10
//   bid   99: #3 x10
//   bid   98: #4 x20
OrderBook deep_book() {
  OrderBook book;
  rest(book, OrderId{1}, bid(Price{100}, Qty{5}));
  rest(book, OrderId{2}, bid(Price{100}, Qty{10}));
  rest(book, OrderId{3}, bid(Price{99}, Qty{10}));
  rest(book, OrderId{4}, bid(Price{98}, Qty{20}));
  rest(book, OrderId{5}, ask(Price{101}, Qty{5}));
  rest(book, OrderId{6}, ask(Price{101}, Qty{10}));
  rest(book, OrderId{7}, ask(Price{102}, Qty{10}));
  rest(book, OrderId{8}, ask(Price{103}, Qty{20}));
  return book;
}

} // namespace

TEST(OrderBookTest, NewOrderBookIsEmpty) {
  OrderBook book;
  EXPECT_TRUE(book.orders().empty());
}

// ---- Order ids --------------------------------------------------------------------------------

TEST(OrderIds, FirstOrderGetsIdOne) {
  OrderBook book;
  Request order = bid(Price{100}, Qty{10});

  EXPECT_EQ(submit(book, order), (Events{Accepted{OrderId{1}, order}}));
}

TEST(OrderIds, FilledOrdersStillUseAnId) {
  OrderBook book;
  Request first = bid(Price{100}, Qty{10});
  Request second = ask(Price{100}, Qty{10}); // fills #1 completely and never rests
  Request third = bid(Price{100}, Qty{10});

  submit(book, first);
  submit(book, second);
  EXPECT_EQ(submit(book, third), (Events{Accepted{OrderId{3}, third}}));
}

TEST(OrderIds, RejectedOrderDoesNotUseAnId) {
  OrderBook book;
  submit(book, bid(Price{100}, Qty{0}));
  Request order = bid(Price{100}, Qty{10});

  EXPECT_EQ(submit(book, order), (Events{Accepted{OrderId{1}, order}}));
}

TEST(OrderIds, CancelDoesNotUseAnId) {
  OrderBook book;
  submit(book, bid(Price{100}, Qty{10}));
  submit(book, Cancel{OrderId{1}});
  Request order = bid(Price{100}, Qty{10});

  EXPECT_EQ(submit(book, order), (Events{Accepted{OrderId{2}, order}}));
}

TEST(OrderIds, EachBookCountsSeparately) {
  OrderBook other = deep_book();
  OrderBook book;
  Request order = bid(Price{100}, Qty{10});

  EXPECT_EQ(submit(book, order), (Events{Accepted{OrderId{1}, order}}));
}

// ---- Resting ----------------------------------------------------------------------------------

TEST(Resting, BidOnEmptyBookRests) {
  OrderBook book;
  Request order = bid(Price{100}, Qty{10});

  EXPECT_EQ(submit(book, order), (Events{Accepted{OrderId{1}, order}}));
  EXPECT_EQ(resting(book, OrderId{1}), (RestingOrder{OrderId{1}, Side::Bid, Price{100}, Qty{10}}));
}

TEST(Resting, AskOnEmptyBookRests) {
  OrderBook book;
  Request order = ask(Price{100}, Qty{10});

  EXPECT_EQ(submit(book, order), (Events{Accepted{OrderId{1}, order}}));
  EXPECT_EQ(resting(book, OrderId{1}), (RestingOrder{OrderId{1}, Side::Ask, Price{100}, Qty{10}}));
}

TEST(Resting, BidBelowBestAskRests) {
  OrderBook book = spread_book();
  Request order = bid(Price{100}, Qty{10});

  EXPECT_EQ(submit(book, order), (Events{Accepted{OrderId{3}, order}}));
  EXPECT_EQ(resting(book, OrderId{3}), (RestingOrder{OrderId{3}, Side::Bid, Price{100}, Qty{10}}));
  EXPECT_EQ(book.orders().size(), 3u);
}

TEST(Resting, AskAboveBestBidRests) {
  OrderBook book = spread_book();
  Request order = ask(Price{100}, Qty{10});

  EXPECT_EQ(submit(book, order), (Events{Accepted{OrderId{3}, order}}));
  EXPECT_EQ(resting(book, OrderId{3}), (RestingOrder{OrderId{3}, Side::Ask, Price{100}, Qty{10}}));
  EXPECT_EQ(book.orders().size(), 3u);
}

// ---- Matching ---------------------------------------------------------------------------------

TEST(Matching, BidFillsEqualSizeAsk) {
  OrderBook book = spread_book();
  Request order = bid(Price{101}, Qty{10});

  EXPECT_EQ(submit(book, order), (Events{
    Accepted{OrderId{3}, order},
    Filled{OrderId{3}, Side::Bid, Price{101}, Qty{10}},
    Filled{OrderId{2}, Side::Ask, Price{101}, Qty{10}},
    Fulfilled{OrderId{3}},
    Fulfilled{OrderId{2}},
  }));
  EXPECT_FALSE(book.orders().contains(OrderId{2}));
  EXPECT_FALSE(book.orders().contains(OrderId{3}));
  EXPECT_TRUE(book.orders().contains(OrderId{1}));
}

TEST(Matching, AskFillsEqualSizeBid) {
  OrderBook book = spread_book();
  Request order = ask(Price{99}, Qty{10});

  EXPECT_EQ(submit(book, order), (Events{
    Accepted{OrderId{3}, order},
    Filled{OrderId{3}, Side::Ask, Price{99}, Qty{10}},
    Filled{OrderId{1}, Side::Bid, Price{99}, Qty{10}},
    Fulfilled{OrderId{3}},
    Fulfilled{OrderId{1}},
  }));
  EXPECT_FALSE(book.orders().contains(OrderId{1}));
  EXPECT_FALSE(book.orders().contains(OrderId{3}));
  EXPECT_TRUE(book.orders().contains(OrderId{2}));
}

TEST(Matching, SmallerBidPartiallyFillsAsk) {
  OrderBook book = spread_book();
  Request order = bid(Price{101}, Qty{4});

  EXPECT_EQ(submit(book, order), (Events{
    Accepted{OrderId{3}, order},
    Filled{OrderId{3}, Side::Bid, Price{101}, Qty{4}},
    Filled{OrderId{2}, Side::Ask, Price{101}, Qty{4}},
    Fulfilled{OrderId{3}},
  }));
  EXPECT_EQ(resting(book, OrderId{2}), (RestingOrder{OrderId{2}, Side::Ask, Price{101}, Qty{6}}));
  EXPECT_FALSE(book.orders().contains(OrderId{3}));
}

TEST(Matching, SmallerAskPartiallyFillsBid) {
  OrderBook book = spread_book();
  Request order = ask(Price{99}, Qty{4});

  EXPECT_EQ(submit(book, order), (Events{
    Accepted{OrderId{3}, order},
    Filled{OrderId{3}, Side::Ask, Price{99}, Qty{4}},
    Filled{OrderId{1}, Side::Bid, Price{99}, Qty{4}},
    Fulfilled{OrderId{3}},
  }));
  EXPECT_EQ(resting(book, OrderId{1}), (RestingOrder{OrderId{1}, Side::Bid, Price{99}, Qty{6}}));
  EXPECT_FALSE(book.orders().contains(OrderId{3}));
}

TEST(Matching, LargerBidFillsAskAndRestsRemainder) {
  OrderBook book = spread_book();
  Request order = bid(Price{101}, Qty{15});

  EXPECT_EQ(submit(book, order), (Events{
    Accepted{OrderId{3}, order},
    Filled{OrderId{3}, Side::Bid, Price{101}, Qty{10}},
    Filled{OrderId{2}, Side::Ask, Price{101}, Qty{10}},
    Fulfilled{OrderId{2}},
  }));
  EXPECT_FALSE(book.orders().contains(OrderId{2}));
  EXPECT_EQ(resting(book, OrderId{3}), (RestingOrder{OrderId{3}, Side::Bid, Price{101}, Qty{5}}));
}

TEST(Matching, LargerAskFillsBidAndRestsRemainder) {
  OrderBook book = spread_book();
  Request order = ask(Price{99}, Qty{15});

  EXPECT_EQ(submit(book, order), (Events{
    Accepted{OrderId{3}, order},
    Filled{OrderId{3}, Side::Ask, Price{99}, Qty{10}},
    Filled{OrderId{1}, Side::Bid, Price{99}, Qty{10}},
    Fulfilled{OrderId{1}},
  }));
  EXPECT_FALSE(book.orders().contains(OrderId{1}));
  EXPECT_EQ(resting(book, OrderId{3}), (RestingOrder{OrderId{3}, Side::Ask, Price{99}, Qty{5}}));
}

TEST(Matching, BidTradesAtRestingAskPrice) {
  OrderBook book = spread_book();
  Request order = bid(Price{105}, Qty{10});

  EXPECT_EQ(submit(book, order), (Events{
    Accepted{OrderId{3}, order},
    Filled{OrderId{3}, Side::Bid, Price{101}, Qty{10}},
    Filled{OrderId{2}, Side::Ask, Price{101}, Qty{10}},
    Fulfilled{OrderId{3}},
    Fulfilled{OrderId{2}},
  }));
}

TEST(Matching, AskTradesAtRestingBidPrice) {
  OrderBook book = spread_book();
  Request order = ask(Price{95}, Qty{10});

  EXPECT_EQ(submit(book, order), (Events{
    Accepted{OrderId{3}, order},
    Filled{OrderId{3}, Side::Ask, Price{99}, Qty{10}},
    Filled{OrderId{1}, Side::Bid, Price{99}, Qty{10}},
    Fulfilled{OrderId{3}},
    Fulfilled{OrderId{1}},
  }));
}

TEST(Matching, PartiallyFilledOrderCanBeFilledAgain) {
  OrderBook book = spread_book();
  submit(book, bid(Price{101}, Qty{4})); // #3; #2 has 6 left
  Request order = bid(Price{101}, Qty{6});

  EXPECT_EQ(submit(book, order), (Events{
    Accepted{OrderId{4}, order},
    Filled{OrderId{4}, Side::Bid, Price{101}, Qty{6}},
    Filled{OrderId{2}, Side::Ask, Price{101}, Qty{6}},
    Fulfilled{OrderId{4}},
    Fulfilled{OrderId{2}},
  }));
  EXPECT_FALSE(book.orders().contains(OrderId{2}));
}

TEST(Matching, RestedRemainderMatchesLaterOrders) {
  OrderBook book = spread_book();
  submit(book, bid(Price{101}, Qty{15})); // #3 fills #2, rests 5 @ 101
  Request order = ask(Price{101}, Qty{5});

  EXPECT_EQ(submit(book, order), (Events{
    Accepted{OrderId{4}, order},
    Filled{OrderId{4}, Side::Ask, Price{101}, Qty{5}},
    Filled{OrderId{3}, Side::Bid, Price{101}, Qty{5}},
    Fulfilled{OrderId{4}},
    Fulfilled{OrderId{3}},
  }));
}

TEST(Matching, FullyFilledOrderNoLongerMatches) {
  OrderBook book = spread_book();
  submit(book, bid(Price{101}, Qty{10})); // #3 fills #2 exactly
  Request order = bid(Price{101}, Qty{10});

  EXPECT_EQ(submit(book, order), (Events{Accepted{OrderId{4}, order}}));
  EXPECT_EQ(resting(book, OrderId{4}), (RestingOrder{OrderId{4}, Side::Bid, Price{101}, Qty{10}}));
}

TEST(Matching, BidSweepsAsksBestPriceFirst) {
  OrderBook book = deep_book();
  Request order = bid(Price{102}, Qty{20});

  EXPECT_EQ(submit(book, order), (Events{
    Accepted{OrderId{9}, order},
    Filled{OrderId{9}, Side::Bid, Price{101}, Qty{5}},
    Filled{OrderId{5}, Side::Ask, Price{101}, Qty{5}},
    Fulfilled{OrderId{5}},
    Filled{OrderId{9}, Side::Bid, Price{101}, Qty{10}},
    Filled{OrderId{6}, Side::Ask, Price{101}, Qty{10}},
    Fulfilled{OrderId{6}},
    Filled{OrderId{9}, Side::Bid, Price{102}, Qty{5}},
    Filled{OrderId{7}, Side::Ask, Price{102}, Qty{5}},
    Fulfilled{OrderId{9}},
  }));
  EXPECT_EQ(resting(book, OrderId{7}), (RestingOrder{OrderId{7}, Side::Ask, Price{102}, Qty{5}}));
  EXPECT_EQ(resting(book, OrderId{8}), (RestingOrder{OrderId{8}, Side::Ask, Price{103}, Qty{20}}));
  EXPECT_FALSE(book.orders().contains(OrderId{9}));
}

TEST(Matching, AskSweepsBidsBestPriceFirst) {
  OrderBook book = deep_book();
  Request order = ask(Price{99}, Qty{20});

  EXPECT_EQ(submit(book, order), (Events{
    Accepted{OrderId{9}, order},
    Filled{OrderId{9}, Side::Ask, Price{100}, Qty{5}},
    Filled{OrderId{1}, Side::Bid, Price{100}, Qty{5}},
    Fulfilled{OrderId{1}},
    Filled{OrderId{9}, Side::Ask, Price{100}, Qty{10}},
    Filled{OrderId{2}, Side::Bid, Price{100}, Qty{10}},
    Fulfilled{OrderId{2}},
    Filled{OrderId{9}, Side::Ask, Price{99}, Qty{5}},
    Filled{OrderId{3}, Side::Bid, Price{99}, Qty{5}},
    Fulfilled{OrderId{9}},
  }));
  EXPECT_EQ(resting(book, OrderId{3}), (RestingOrder{OrderId{3}, Side::Bid, Price{99}, Qty{5}}));
  EXPECT_EQ(resting(book, OrderId{4}), (RestingOrder{OrderId{4}, Side::Bid, Price{98}, Qty{20}}));
  EXPECT_FALSE(book.orders().contains(OrderId{9}));
}

TEST(Matching, BidSweepStopsAtLimitPrice) {
  OrderBook book = deep_book();
  Request order = bid(Price{101}, Qty{20});

  EXPECT_EQ(submit(book, order), (Events{
    Accepted{OrderId{9}, order},
    Filled{OrderId{9}, Side::Bid, Price{101}, Qty{5}},
    Filled{OrderId{5}, Side::Ask, Price{101}, Qty{5}},
    Fulfilled{OrderId{5}},
    Filled{OrderId{9}, Side::Bid, Price{101}, Qty{10}},
    Filled{OrderId{6}, Side::Ask, Price{101}, Qty{10}},
    Fulfilled{OrderId{6}},
  }));
  EXPECT_EQ(resting(book, OrderId{9}), (RestingOrder{OrderId{9}, Side::Bid, Price{101}, Qty{5}}));
  EXPECT_EQ(resting(book, OrderId{7}), (RestingOrder{OrderId{7}, Side::Ask, Price{102}, Qty{10}}));
}

TEST(Matching, AskSweepStopsAtLimitPrice) {
  OrderBook book = deep_book();
  Request order = ask(Price{100}, Qty{20});

  EXPECT_EQ(submit(book, order), (Events{
    Accepted{OrderId{9}, order},
    Filled{OrderId{9}, Side::Ask, Price{100}, Qty{5}},
    Filled{OrderId{1}, Side::Bid, Price{100}, Qty{5}},
    Fulfilled{OrderId{1}},
    Filled{OrderId{9}, Side::Ask, Price{100}, Qty{10}},
    Filled{OrderId{2}, Side::Bid, Price{100}, Qty{10}},
    Fulfilled{OrderId{2}},
  }));
  EXPECT_EQ(resting(book, OrderId{9}), (RestingOrder{OrderId{9}, Side::Ask, Price{100}, Qty{5}}));
  EXPECT_EQ(resting(book, OrderId{3}), (RestingOrder{OrderId{3}, Side::Bid, Price{99}, Qty{10}}));
}

// ---- Time priority ----------------------------------------------------------------------------

TEST(TimePriority, EarlierBidAtSamePriceFillsFirst) {
  OrderBook book = deep_book();
  Request order = ask(Price{100}, Qty{3});

  EXPECT_EQ(submit(book, order), (Events{
    Accepted{OrderId{9}, order},
    Filled{OrderId{9}, Side::Ask, Price{100}, Qty{3}},
    Filled{OrderId{1}, Side::Bid, Price{100}, Qty{3}},
    Fulfilled{OrderId{9}},
  }));
  EXPECT_EQ(resting(book, OrderId{1}), (RestingOrder{OrderId{1}, Side::Bid, Price{100}, Qty{2}}));
  EXPECT_EQ(resting(book, OrderId{2}), (RestingOrder{OrderId{2}, Side::Bid, Price{100}, Qty{10}}));
}

TEST(TimePriority, EarlierAskAtSamePriceFillsFirst) {
  OrderBook book = deep_book();
  Request order = bid(Price{101}, Qty{3});

  EXPECT_EQ(submit(book, order), (Events{
    Accepted{OrderId{9}, order},
    Filled{OrderId{9}, Side::Bid, Price{101}, Qty{3}},
    Filled{OrderId{5}, Side::Ask, Price{101}, Qty{3}},
    Fulfilled{OrderId{9}},
  }));
  EXPECT_EQ(resting(book, OrderId{5}), (RestingOrder{OrderId{5}, Side::Ask, Price{101}, Qty{2}}));
  EXPECT_EQ(resting(book, OrderId{6}), (RestingOrder{OrderId{6}, Side::Ask, Price{101}, Qty{10}}));
}

TEST(TimePriority, PartiallyFilledOrderKeepsItsPlace) {
  OrderBook book = deep_book();
  submit(book, ask(Price{100}, Qty{3})); // #9; #1 has 2 left
  Request order = ask(Price{100}, Qty{2});

  EXPECT_EQ(submit(book, order), (Events{
    Accepted{OrderId{10}, order},
    Filled{OrderId{10}, Side::Ask, Price{100}, Qty{2}},
    Filled{OrderId{1}, Side::Bid, Price{100}, Qty{2}},
    Fulfilled{OrderId{10}},
    Fulfilled{OrderId{1}},
  }));
  EXPECT_EQ(resting(book, OrderId{2}), (RestingOrder{OrderId{2}, Side::Bid, Price{100}, Qty{10}}));
}

TEST(TimePriority, NewOrderJoinsBackOfLevel) {
  OrderBook book = spread_book();
  rest(book, OrderId{3}, bid(Price{99}, Qty{10}));
  Request order = ask(Price{99}, Qty{10});

  EXPECT_EQ(submit(book, order), (Events{
    Accepted{OrderId{4}, order},
    Filled{OrderId{4}, Side::Ask, Price{99}, Qty{10}},
    Filled{OrderId{1}, Side::Bid, Price{99}, Qty{10}},
    Fulfilled{OrderId{4}},
    Fulfilled{OrderId{1}},
  }));
  EXPECT_EQ(resting(book, OrderId{3}), (RestingOrder{OrderId{3}, Side::Bid, Price{99}, Qty{10}}));
}

// ---- Cancelling -------------------------------------------------------------------------------

TEST(Cancelling, CancelRemovesRestingOrder) {
  OrderBook book = spread_book();

  EXPECT_EQ(submit(book, Cancel{OrderId{1}}), (Events{Cancelled{OrderId{1}}}));
  EXPECT_FALSE(book.orders().contains(OrderId{1}));
  EXPECT_TRUE(book.orders().contains(OrderId{2}));
}

TEST(Cancelling, CancelledOrderNoLongerMatches) {
  OrderBook book = spread_book();
  submit(book, Cancel{OrderId{2}});
  Request order = bid(Price{101}, Qty{10});

  EXPECT_EQ(submit(book, order), (Events{Accepted{OrderId{3}, order}}));
  EXPECT_EQ(resting(book, OrderId{3}), (RestingOrder{OrderId{3}, Side::Bid, Price{101}, Qty{10}}));
}

TEST(Cancelling, CancelKeepsOrderOfRemainingOrdersAtLevel) {
  OrderBook book = deep_book();
  submit(book, Cancel{OrderId{1}});
  Request order = ask(Price{100}, Qty{3});

  EXPECT_EQ(submit(book, order), (Events{
    Accepted{OrderId{9}, order},
    Filled{OrderId{9}, Side::Ask, Price{100}, Qty{3}},
    Filled{OrderId{2}, Side::Bid, Price{100}, Qty{3}},
    Fulfilled{OrderId{9}},
  }));
}

TEST(Cancelling, CancelPartiallyFilledOrderRemovesRemainder) {
  OrderBook book = spread_book();
  submit(book, bid(Price{101}, Qty{4})); // #2 has 6 left

  EXPECT_EQ(submit(book, Cancel{OrderId{2}}), (Events{Cancelled{OrderId{2}}}));
  EXPECT_FALSE(book.orders().contains(OrderId{2}));
}

TEST(Cancelling, CancelUnknownIdIsRejected) {
  OrderBook book = spread_book();
  Request cancel = Cancel{OrderId{99}};

  EXPECT_EQ(submit(book, cancel), (Events{Rejected{cancel, Rejected::Reason::OrderIdNotFound}}));
  EXPECT_EQ(book.orders(), spread_book().orders());
}

TEST(Cancelling, CancelFilledOrderIsRejected) {
  OrderBook book = spread_book();
  submit(book, bid(Price{101}, Qty{10})); // fills #2
  Request cancel = Cancel{OrderId{2}};

  EXPECT_EQ(submit(book, cancel), (Events{Rejected{cancel, Rejected::Reason::OrderIdNotFound}}));
}

TEST(Cancelling, CancelTwiceIsRejected) {
  OrderBook book = spread_book();
  submit(book, Cancel{OrderId{1}});
  Request cancel = Cancel{OrderId{1}};

  EXPECT_EQ(submit(book, cancel), (Events{Rejected{cancel, Rejected::Reason::OrderIdNotFound}}));
}

// ---- Market orders ----------------------------------------------------------------------------

TEST(MarketOrder, BidFillsAcrossLevels) {
  OrderBook book = deep_book();
  Request order = NewMarket{Side::Bid, Qty{20}, MarketTif::ImmediateOrCancel};

  EXPECT_EQ(submit(book, order), (Events{
    Accepted{OrderId{9}, order},
    Filled{OrderId{9}, Side::Bid, Price{101}, Qty{5}},
    Filled{OrderId{5}, Side::Ask, Price{101}, Qty{5}},
    Fulfilled{OrderId{5}},
    Filled{OrderId{9}, Side::Bid, Price{101}, Qty{10}},
    Filled{OrderId{6}, Side::Ask, Price{101}, Qty{10}},
    Fulfilled{OrderId{6}},
    Filled{OrderId{9}, Side::Bid, Price{102}, Qty{5}},
    Filled{OrderId{7}, Side::Ask, Price{102}, Qty{5}},
    Fulfilled{OrderId{9}},
  }));
  EXPECT_EQ(resting(book, OrderId{7}), (RestingOrder{OrderId{7}, Side::Ask, Price{102}, Qty{5}}));
}

TEST(MarketOrder, AskFillsAcrossLevels) {
  OrderBook book = deep_book();
  Request order = NewMarket{Side::Ask, Qty{20}, MarketTif::ImmediateOrCancel};

  EXPECT_EQ(submit(book, order), (Events{
    Accepted{OrderId{9}, order},
    Filled{OrderId{9}, Side::Ask, Price{100}, Qty{5}},
    Filled{OrderId{1}, Side::Bid, Price{100}, Qty{5}},
    Fulfilled{OrderId{1}},
    Filled{OrderId{9}, Side::Ask, Price{100}, Qty{10}},
    Filled{OrderId{2}, Side::Bid, Price{100}, Qty{10}},
    Fulfilled{OrderId{2}},
    Filled{OrderId{9}, Side::Ask, Price{99}, Qty{5}},
    Filled{OrderId{3}, Side::Bid, Price{99}, Qty{5}},
    Fulfilled{OrderId{9}},
  }));
  EXPECT_EQ(resting(book, OrderId{3}), (RestingOrder{OrderId{3}, Side::Bid, Price{99}, Qty{5}}));
}

TEST(MarketOrder, RemainderIsCancelledWhenSideRunsOut) {
  OrderBook book = deep_book();
  Request order = NewMarket{Side::Bid, Qty{50}, MarketTif::ImmediateOrCancel};

  EXPECT_EQ(submit(book, order), (Events{
    Accepted{OrderId{9}, order},
    Filled{OrderId{9}, Side::Bid, Price{101}, Qty{5}},
    Filled{OrderId{5}, Side::Ask, Price{101}, Qty{5}},
    Fulfilled{OrderId{5}},
    Filled{OrderId{9}, Side::Bid, Price{101}, Qty{10}},
    Filled{OrderId{6}, Side::Ask, Price{101}, Qty{10}},
    Fulfilled{OrderId{6}},
    Filled{OrderId{9}, Side::Bid, Price{102}, Qty{10}},
    Filled{OrderId{7}, Side::Ask, Price{102}, Qty{10}},
    Fulfilled{OrderId{7}},
    Filled{OrderId{9}, Side::Bid, Price{103}, Qty{20}},
    Filled{OrderId{8}, Side::Ask, Price{103}, Qty{20}},
    Fulfilled{OrderId{8}},
    Cancelled{OrderId{9}},
  }));
  EXPECT_FALSE(book.orders().contains(OrderId{9}));
  EXPECT_EQ(book.orders().size(), 4u); // only the bids are left
}

TEST(MarketOrder, OrderAgainstEmptySideIsCancelled) {
  OrderBook book;
  rest(book, OrderId{1}, bid(Price{99}, Qty{10}));
  Request order = NewMarket{Side::Bid, Qty{10}, MarketTif::ImmediateOrCancel};

  EXPECT_EQ(submit(book, order), (Events{Accepted{OrderId{2}, order}, Cancelled{OrderId{2}}}));
  EXPECT_FALSE(book.orders().contains(OrderId{2}));
}

TEST(MarketOrder, FokFillsWhenEnoughQtyRests) {
  OrderBook book = deep_book();
  Request order = NewMarket{Side::Bid, Qty{20}, MarketTif::FillOrKill};

  EXPECT_EQ(submit(book, order), (Events{
    Accepted{OrderId{9}, order},
    Filled{OrderId{9}, Side::Bid, Price{101}, Qty{5}},
    Filled{OrderId{5}, Side::Ask, Price{101}, Qty{5}},
    Fulfilled{OrderId{5}},
    Filled{OrderId{9}, Side::Bid, Price{101}, Qty{10}},
    Filled{OrderId{6}, Side::Ask, Price{101}, Qty{10}},
    Fulfilled{OrderId{6}},
    Filled{OrderId{9}, Side::Bid, Price{102}, Qty{5}},
    Filled{OrderId{7}, Side::Ask, Price{102}, Qty{5}},
    Fulfilled{OrderId{9}},
  }));
}

TEST(MarketOrder, FokIsKilledWhenNotEnoughQtyRests) {
  OrderBook book = deep_book();
  Request order = NewMarket{Side::Bid, Qty{46}, MarketTif::FillOrKill}; // 45 rests

  EXPECT_EQ(submit(book, order), (Events{Accepted{OrderId{9}, order}, Cancelled{OrderId{9}}}));
  EXPECT_EQ(book.orders(), deep_book().orders());
}

// ---- Immediate or cancel ----------------------------------------------------------------------

TEST(ImmediateOrCancel, FullyFilledOrderIsFulfilled) {
  OrderBook book = deep_book();
  Request order = NewLimit{Side::Bid, Price{102}, Qty{20}, Tif::ImmediateOrCancel};

  EXPECT_EQ(submit(book, order), (Events{
    Accepted{OrderId{9}, order},
    Filled{OrderId{9}, Side::Bid, Price{101}, Qty{5}},
    Filled{OrderId{5}, Side::Ask, Price{101}, Qty{5}},
    Fulfilled{OrderId{5}},
    Filled{OrderId{9}, Side::Bid, Price{101}, Qty{10}},
    Filled{OrderId{6}, Side::Ask, Price{101}, Qty{10}},
    Fulfilled{OrderId{6}},
    Filled{OrderId{9}, Side::Bid, Price{102}, Qty{5}},
    Filled{OrderId{7}, Side::Ask, Price{102}, Qty{5}},
    Fulfilled{OrderId{9}},
  }));
}

TEST(ImmediateOrCancel, BidRemainderIsCancelledNotRested) {
  OrderBook book = deep_book();
  Request order = NewLimit{Side::Bid, Price{101}, Qty{20}, Tif::ImmediateOrCancel};

  EXPECT_EQ(submit(book, order), (Events{
    Accepted{OrderId{9}, order},
    Filled{OrderId{9}, Side::Bid, Price{101}, Qty{5}},
    Filled{OrderId{5}, Side::Ask, Price{101}, Qty{5}},
    Fulfilled{OrderId{5}},
    Filled{OrderId{9}, Side::Bid, Price{101}, Qty{10}},
    Filled{OrderId{6}, Side::Ask, Price{101}, Qty{10}},
    Fulfilled{OrderId{6}},
    Cancelled{OrderId{9}},
  }));
  EXPECT_FALSE(book.orders().contains(OrderId{9}));
  EXPECT_EQ(resting(book, OrderId{7}), (RestingOrder{OrderId{7}, Side::Ask, Price{102}, Qty{10}}));
}

TEST(ImmediateOrCancel, AskRemainderIsCancelledNotRested) {
  OrderBook book = deep_book();
  Request order = NewLimit{Side::Ask, Price{100}, Qty{20}, Tif::ImmediateOrCancel};

  EXPECT_EQ(submit(book, order), (Events{
    Accepted{OrderId{9}, order},
    Filled{OrderId{9}, Side::Ask, Price{100}, Qty{5}},
    Filled{OrderId{1}, Side::Bid, Price{100}, Qty{5}},
    Fulfilled{OrderId{1}},
    Filled{OrderId{9}, Side::Ask, Price{100}, Qty{10}},
    Filled{OrderId{2}, Side::Bid, Price{100}, Qty{10}},
    Fulfilled{OrderId{2}},
    Cancelled{OrderId{9}},
  }));
  EXPECT_FALSE(book.orders().contains(OrderId{9}));
  EXPECT_EQ(resting(book, OrderId{3}), (RestingOrder{OrderId{3}, Side::Bid, Price{99}, Qty{10}}));
}

TEST(ImmediateOrCancel, OrderWithNoMatchIsCancelled) {
  OrderBook book = spread_book();
  Request order = NewLimit{Side::Bid, Price{100}, Qty{10}, Tif::ImmediateOrCancel};

  EXPECT_EQ(submit(book, order), (Events{Accepted{OrderId{3}, order}, Cancelled{OrderId{3}}}));
  EXPECT_EQ(book.orders(), spread_book().orders());
}

// ---- Fill or kill -----------------------------------------------------------------------------

TEST(FillOrKill, BidFillsWhenQtyWithinLimitExactlyMatches) {
  OrderBook book = deep_book();
  Request order = NewLimit{Side::Bid, Price{102}, Qty{25}, Tif::FillOrKill};

  EXPECT_EQ(submit(book, order), (Events{
    Accepted{OrderId{9}, order},
    Filled{OrderId{9}, Side::Bid, Price{101}, Qty{5}},
    Filled{OrderId{5}, Side::Ask, Price{101}, Qty{5}},
    Fulfilled{OrderId{5}},
    Filled{OrderId{9}, Side::Bid, Price{101}, Qty{10}},
    Filled{OrderId{6}, Side::Ask, Price{101}, Qty{10}},
    Fulfilled{OrderId{6}},
    Filled{OrderId{9}, Side::Bid, Price{102}, Qty{10}},
    Filled{OrderId{7}, Side::Ask, Price{102}, Qty{10}},
    Fulfilled{OrderId{9}},
    Fulfilled{OrderId{7}},
  }));
  EXPECT_EQ(resting(book, OrderId{8}), (RestingOrder{OrderId{8}, Side::Ask, Price{103}, Qty{20}}));
}

TEST(FillOrKill, AskFillsWhenQtyWithinLimitExactlyMatches) {
  OrderBook book = deep_book();
  Request order = NewLimit{Side::Ask, Price{99}, Qty{25}, Tif::FillOrKill};

  EXPECT_EQ(submit(book, order), (Events{
    Accepted{OrderId{9}, order},
    Filled{OrderId{9}, Side::Ask, Price{100}, Qty{5}},
    Filled{OrderId{1}, Side::Bid, Price{100}, Qty{5}},
    Fulfilled{OrderId{1}},
    Filled{OrderId{9}, Side::Ask, Price{100}, Qty{10}},
    Filled{OrderId{2}, Side::Bid, Price{100}, Qty{10}},
    Fulfilled{OrderId{2}},
    Filled{OrderId{9}, Side::Ask, Price{99}, Qty{10}},
    Filled{OrderId{3}, Side::Bid, Price{99}, Qty{10}},
    Fulfilled{OrderId{9}},
    Fulfilled{OrderId{3}},
  }));
  EXPECT_EQ(resting(book, OrderId{4}), (RestingOrder{OrderId{4}, Side::Bid, Price{98}, Qty{20}}));
}

// The book has more than enough ask qty in total, but only 25 at or below the limit price.
TEST(FillOrKill, BidIsKilledWhenNotEnoughQtyWithinLimit) {
  OrderBook book = deep_book();
  Request order = NewLimit{Side::Bid, Price{102}, Qty{26}, Tif::FillOrKill};

  EXPECT_EQ(submit(book, order), (Events{Accepted{OrderId{9}, order}, Cancelled{OrderId{9}}}));
  EXPECT_EQ(book.orders(), deep_book().orders());
}

TEST(FillOrKill, AskIsKilledWhenNotEnoughQtyWithinLimit) {
  OrderBook book = deep_book();
  Request order = NewLimit{Side::Ask, Price{99}, Qty{26}, Tif::FillOrKill};

  EXPECT_EQ(submit(book, order), (Events{Accepted{OrderId{9}, order}, Cancelled{OrderId{9}}}));
  EXPECT_EQ(book.orders(), deep_book().orders());
}

TEST(FillOrKill, OrderWithNoMatchIsKilled) {
  OrderBook book = spread_book();
  Request order = NewLimit{Side::Bid, Price{100}, Qty{10}, Tif::FillOrKill};

  EXPECT_EQ(submit(book, order), (Events{Accepted{OrderId{3}, order}, Cancelled{OrderId{3}}}));
  EXPECT_EQ(book.orders(), spread_book().orders());
}

// ---- Validation -------------------------------------------------------------------------------

TEST(Validation, ZeroQtyLimitIsRejected) {
  OrderBook book = spread_book();
  Request order = bid(Price{101}, Qty{0}); // would cross if it had qty

  EXPECT_EQ(submit(book, order), (Events{Rejected{order, Rejected::Reason::ZeroQty}}));
  EXPECT_EQ(book.orders(), spread_book().orders());
}

TEST(Validation, ZeroQtyMarketIsRejected) {
  OrderBook book = spread_book();
  Request order = NewMarket{Side::Bid, Qty{0}, MarketTif::ImmediateOrCancel};

  EXPECT_EQ(submit(book, order), (Events{Rejected{order, Rejected::Reason::ZeroQty}}));
  EXPECT_EQ(book.orders(), spread_book().orders());
}
