#include <gtest/gtest.h>

#include "builders.hpp"
#include "parse.hpp"

// #include "parse.hpp"

TEST(ParseRequestTest, NewLimitSell) {
  EXPECT_EQ(parse_request("SELL 10 @ 10125"), ask(Price(10125), Qty(10)));
}

TEST(ParseRequestTest, NewLimitBuy) {
  EXPECT_EQ(parse_request("BUY 5 @ 10124"), bid(Price(10124), Qty(5)));
}

TEST(ParseRequestTest, NewMarketSell) {
  EXPECT_EQ(parse_request("SELL 20 MARKET"), askm(Qty(20)));
}

TEST(ParseRequestTest, NewMarketBuy) {
  EXPECT_EQ(parse_request("BUY 40 MARKET"), bidm(Qty(40)));
}

TEST(ParseRequestTest, Cancel) {
  EXPECT_EQ(parse_request("CANCEL 2"), Request(Cancel(OrderId(2))));
}

// Events

TEST(ParseEventTest, AcceptedLimitSell) {
  EXPECT_EQ(parse_event("ACCEPTED 1 SELL 10 @ 10125"),
            Event(Accepted(OrderId(1), ask(Price(10125), Qty(10)))));
}

TEST(ParseEventTest, AcceptedLimitBuy) {
  EXPECT_EQ(parse_event("ACCEPTED 2 BUY 5 @ 10124"),
            Event(Accepted(OrderId(2), bid(Price(10124), Qty(5)))));
}

TEST(ParseEventTest, AcceptedMarketSell) {
  EXPECT_EQ(parse_event("ACCEPTED 1 SELL 20 MARKET"),
            Event(Accepted(OrderId(1), askm(Qty(20)))));
}

TEST(ParseEventTest, AcceptedMarketBuy) {
  EXPECT_EQ(parse_event("ACCEPTED 2 BUY 40 MARKET"),
            Event(Accepted(OrderId(2), bidm(Qty(40)))));
}

TEST(ParseEventTest, AcceptedCancel) {
  EXPECT_EQ(parse_event("ACCEPTED 3 CANCEL 1"),
            Event(Accepted(OrderId(3), Cancel(OrderId(1)))));
}

TEST(ParseEventTest, FilledAsk) {
  EXPECT_EQ(parse_event("FILLED 1 ASK 10 @ 10125"),
            Event(Filled(OrderId(1), Side::Ask, Price(10125), Qty(10))));
}

TEST(ParseEventTest, FilledBid) {
  EXPECT_EQ(parse_event("FILLED 2 BID 5 @ 10124"),
            Event(Filled(OrderId(2), Side::Bid, Price(10124), Qty(5))));
}

TEST(ParseEventTest, Fulfilled) {
  EXPECT_EQ(parse_event("FULFILLED 4"), Event(Fulfilled(OrderId(4))));
}

TEST(ParseEventTest, Canceled) {
  EXPECT_EQ(parse_event("CANCELED 7"), Event(Canceled(OrderId(7))));
}

TEST(ParseEventTest, RejectedLimitInvalidQty) {
  EXPECT_EQ(parse_event("REJECTED BUY 0 @ 10124 INVALID QTY"),
            Event(Rejected(bid(Price(10124), Qty(0)),
                           Rejected::Reason::InvalidQty)));
}

TEST(ParseEventTest, RejectedMarketInvalidQty) {
  EXPECT_EQ(parse_event("REJECTED SELL 0 MARKET INVALID QTY"),
            Event(Rejected(askm(Qty(0)), Rejected::Reason::InvalidQty)));
}

TEST(ParseEventTest, RejectedCancelNotFound) {
  EXPECT_EQ(parse_event("REJECTED CANCEL 99 NOT FOUND"),
            Event(Rejected(Cancel(OrderId(99)),
                           Rejected::Reason::OrderIdNotFound)));
}
