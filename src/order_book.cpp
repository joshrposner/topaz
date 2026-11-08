#include <algorithm>
#include <topaz/defs.hpp>
#include <topaz/order_book.hpp>
#include <utility>

namespace tpz {

OrderBook::OrderBook() noexcept : next_id_(OrderId(1)) {}

void OrderBook::submit(Request request) {
  std::visit(VisitHelper([&](auto order) { dispatch(order, nullptr); },
                         [&](Cancel c) { cancel(c, nullptr); }),
             request);
}

void OrderBook::submit(Request request, std::vector<Event>& out) {
  std::visit(VisitHelper([&](auto order) { dispatch(order, &out); },
                         [&](Cancel c) { cancel(c, &out); }),
             request);
}

const std::unordered_map<OrderId, RestingOrder>& OrderBook::orders() const {
  return orders_;
}

namespace {
// Per-side implementation of `OrderBook::levels`. `Book` is one side's map.
template <class Book>
std::vector<Level> levels_of(
    const Book& book, const std::unordered_map<OrderId, RestingOrder>& orders) {
  std::vector<Level> out;
  out.reserve(book.size());
  for (const auto& [price, ids] : book) {
    Qty total;
    for (OrderId id : ids) {
      auto it = orders.find(id);
      TPZ_ASSERT(it != orders.end());  // this would be an internal error
      total += it->second.qty;
    }
    out.push_back(Level(price, total));
  }
  return out;
}
}  // namespace

std::vector<Level> OrderBook::levels(Side side) const {
  switch (side) {
    case Side::Ask:
      return levels_of(sell_orders_, orders_);
    case Side::Bid:
      return levels_of(buy_orders_, orders_);
  }
  std::unreachable();
}

OrderId OrderBook::gen_id() {
  next_id_ += OrderId(1);
  return next_id_ - OrderId(1);
}

namespace {
// Per-side implementation of the level removal in `OrderBook::cancel`. `Book`
// is the order's own side's map. Removes the level if it becomes empty. No
// emissions.
template <class Book>
void remove_from_level(Book& book, Price price, OrderId id) {
  auto itp = book.find(price);
  TPZ_ASSERT(itp != book.end());  // this would be an internal error

  std::list<OrderId>& ids = itp->second;
  auto ito = std::find(ids.begin(), ids.end(), id);
  TPZ_ASSERT(ito != ids.end());  // this would be an internal error

  ids.erase(ito);
  if (ids.empty()) book.erase(itp);
}
}  // namespace

// Responsible only for emission of `Rejected` or `Cancelled`
void OrderBook::cancel(Cancel c, std::vector<Event>* out) {
  auto it = orders_.find(c.id);

  if (it == orders_.end()) {
    emit(out, Rejected(c, Rejected::Reason::OrderIdNotFound));
    return;
  }

  RestingOrder order = it->second;

  switch (order.side) {
    case Side::Ask:
      remove_from_level(sell_orders_, order.price, order.id);
      break;
    case Side::Bid:
      remove_from_level(buy_orders_, order.price, order.id);
      break;
  }

  orders_.erase(it);

  emit(out, Canceled(order.id));
}

void OrderBook::emit(std::vector<Event>* out, Event e) {
  if (out) out->push_back(e);
}

}  // namespace tpz
