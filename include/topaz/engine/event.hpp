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

struct Canceled {
  OrderId id;

  friend bool operator==(const Canceled&, const Canceled&) = default;
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

using Event = std::variant<Accepted, Filled, Fulfilled, Canceled, Rejected>;

}  // namespace tpz::eng
