#include <ostream>
#include <utility>
#include <variant>
#include <vector>

#include <topaz/engine/order_book.hpp>

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

using Events = std::vector<Event>;
using Levels = std::vector<Level>;

namespace tpz {

template <typename Tag>
std::ostream& operator<<(std::ostream& os, StrongInt<Tag> v) {
  return os << v.val();
}

std::ostream& operator<<(std::ostream& os, Side side) {
  return os << (side == Side::Bid ? "Bid" : "Ask");
}

std::ostream& operator<<(std::ostream& os, Tif tif) {
  switch (tif) {
    case Tif::GoodTilCancel:
      return os << "GTC";
    case Tif::ImmediateOrCancel:
      return os << "IOC";
    case Tif::FillOrKill:
      return os << "FOK";
  }
  return os << "Tif(" << std::to_underlying(tif) << ')';
}

std::ostream& operator<<(std::ostream& os, MarketTif tif) {
  switch (tif) {
    case MarketTif::ImmediateOrCancel:
      return os << "IOC";
    case MarketTif::FillOrKill:
      return os << "FOK";
  }
  return os << "MarketTif(" << std::to_underlying(tif) << ')';
}

std::ostream& operator<<(std::ostream& os, const NewLimit& o) {
  return os << "NewLimit{" << o.side << ' ' << o.qty << " @ " << o.price << ' '
            << o.tif << '}';
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
  return os << "RestingOrder{#" << o.id << ' ' << o.side << ' ' << o.qty
            << " @ " << o.price << '}';
}

std::ostream& operator<<(std::ostream& os, const Level& l) {
  return os << "Level{" << l.qty << " @ " << l.price << '}';
}

} // namespace tpz

namespace tpz::eng {

std::ostream& operator<<(std::ostream& os, const Accepted& e) {
  return os << "Accepted{#" << e.id << ' ' << e.request << '}';
}

std::ostream& operator<<(std::ostream& os, const Filled& e) {
  return os << "Filled{#" << e.id << ' ' << e.side << ' ' << e.qty << " @ "
            << e.price << '}';
}

std::ostream& operator<<(std::ostream& os, const Fulfilled& e) {
  return os << "Fulfilled{#" << e.id << '}';
}

std::ostream& operator<<(std::ostream& os, const Canceled& e) {
  return os << "Canceled{#" << e.id << '}';
}

std::ostream& operator<<(std::ostream& os, Rejected::Reason reason) {
  switch (reason) {
    case Rejected::Reason::InvalidQty:
      return os << "InvalidQty";
    case Rejected::Reason::OrderIdNotFound:
      return os << "OrderIdNotFound";
  }
  return os << "Reason(" << std::to_underlying(reason) << ')';
}

std::ostream& operator<<(std::ostream& os, const Rejected& e) {
  return os << "Rejected{" << e.request << ", " << e.reason << '}';
}

// Prints event lists as { A, B, ... }. GoogleTest's built-in std::variant
// printer adds the alternative's type name and index to every element, and it
// can't be overridden per element (it's a class specialization that never
// looks for PrintTo), so this overrides printing of the whole vector instead.
void PrintTo(const std::vector<Event>& events, std::ostream* os) {
  *os << "{ ";
  for (bool first = true; const Event& e : events) {
    if (!first) *os << ", ";
    first = false;
    std::visit([&](const auto& alt) { *os << alt; }, e);
  }
  *os << " }";
}

}  // namespace tpz::eng
