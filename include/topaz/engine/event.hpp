#pragma once

#include "../types.hpp"

namespace tpz::eng {

struct Accepted {
  OrderId id;  // assigned by the book
  Request request;

  friend bool operator==(const Accepted&, const Accepted&) = default;
};

struct Filled {
  OrderId id;
  Side side;
  Price price;
  Qty qty;

  friend bool operator==(const Filled&, const Filled&) = default;
};

struct Fulfilled {
  OrderId id;

  friend bool operator==(const Fulfilled&, const Fulfilled&) = default;
};

struct Cancelled {
  OrderId id;

  friend bool operator==(const Cancelled&, const Cancelled&) = default;
};

struct Rejected {
  enum class Reason {
    ZeroQty,
    OrderIdNotFound,
  };
  Request request;
  Reason reason;

  friend bool operator==(const Rejected&, const Rejected&) = default;
};

using Event = std::variant<Accepted, Filled, Fulfilled, Cancelled, Rejected>;

}  // namespace tpz::eng
