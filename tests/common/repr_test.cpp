#include <gtest/gtest.h>

#include "builders.hpp"
#include "repr.hpp"

// Requests

TEST(ReprTest, NewLimitSell) {
  EXPECT_EQ("SELL 10 @ 10125", repr(ask(Price(10125), Qty(10))));
}

TEST(ReprTest, NewLimitBuy) {
  EXPECT_EQ("BUY 5 @ 10124", repr(bid(Price(10124), Qty(5))));
}

TEST(ReprTest, NewMarketSell) {
  EXPECT_EQ("SELL 20 MARKET", repr(askm(Qty(20))));
}

TEST(ReprTest, NewMarketBuy) {
  EXPECT_EQ("BUY 40 MARKET", repr(bidm(Qty(40))));
}

TEST(ReprTest, Cancel) {
  EXPECT_EQ("CANCEL 2", repr(Cancel(OrderId(2))));
}

// Rejection reasons

TEST(ReprTest, ReasonInvalidQty) {
  EXPECT_EQ("INVALID QTY", repr(Rejected::Reason::InvalidQty));
}

TEST(ReprTest, ReasonOrderIdNotFound) {
  EXPECT_EQ("NOT FOUND", repr(Rejected::Reason::OrderIdNotFound));
}

// Events

TEST(ReprTest, AcceptedLimitSell) {
  EXPECT_EQ("ACCEPTED 1 SELL 10 @ 10125",
            repr(Accepted(OrderId(1), ask(Price(10125), Qty(10)))));
}

TEST(ReprTest, AcceptedLimitBuy) {
  EXPECT_EQ("ACCEPTED 2 BUY 5 @ 10124",
            repr(Accepted(OrderId(2), bid(Price(10124), Qty(5)))));
}

TEST(ReprTest, AcceptedMarketSell) {
  EXPECT_EQ("ACCEPTED 1 SELL 20 MARKET",
            repr(Accepted(OrderId(1), askm(Qty(20)))));
}

TEST(ReprTest, AcceptedMarketBuy) {
  EXPECT_EQ("ACCEPTED 2 BUY 40 MARKET",
            repr(Accepted(OrderId(2), bidm(Qty(40)))));
}

TEST(ReprTest, AcceptedCancel) {
  EXPECT_EQ("ACCEPTED 3 CANCEL 1",
            repr(Accepted(OrderId(3), Cancel(OrderId(1)))));
}

TEST(ReprTest, FilledAsk) {
  EXPECT_EQ("FILLED 1 ASK 10 @ 10125",
            repr(Filled(OrderId(1), Side::Ask, Price(10125), Qty(10))));
}

TEST(ReprTest, FilledBid) {
  EXPECT_EQ("FILLED 2 BID 5 @ 10124",
            repr(Filled(OrderId(2), Side::Bid, Price(10124), Qty(5))));
}

TEST(ReprTest, Fulfilled) {
  EXPECT_EQ("FULFILLED 4", repr(Fulfilled(OrderId(4))));
}

TEST(ReprTest, Canceled) {
  EXPECT_EQ("CANCELED 7", repr(Canceled(OrderId(7))));
}

TEST(ReprTest, RejectedLimitInvalidQty) {
  EXPECT_EQ("REJECTED BUY 0 @ 10124 INVALID QTY",
            repr(Rejected(bid(Price(10124), Qty(0)),
                          Rejected::Reason::InvalidQty)));
}

TEST(ReprTest, RejectedMarketInvalidQty) {
  EXPECT_EQ("REJECTED SELL 0 MARKET INVALID QTY",
            repr(Rejected(askm(Qty(0)), Rejected::Reason::InvalidQty)));
}

TEST(ReprTest, RejectedCancelNotFound) {
  EXPECT_EQ("REJECTED CANCEL 99 NOT FOUND",
            repr(Rejected(Cancel(OrderId(99)), Rejected::Reason::OrderIdNotFound)));
}
