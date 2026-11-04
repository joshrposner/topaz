#include <algorithm>
#include <topaz/engine/order_book.hpp>
#include <topaz/defs.hpp>

#include <utility>

namespace tpz::eng {

OrderBook::OrderBook() noexcept : next_id_(OrderId(1)) {}

void OrderBook::submit(Request request) {
  std::visit(VisitHelper {
    [&](auto order) {
      dispatch(order, nullptr);
    },
    [&](Cancel c) {
      cancel(c, nullptr);
    },
  }, request);
}

void OrderBook::submit(Request request, std::vector<Event>& out) {
  std::visit(VisitHelper {
    [&](auto order) {
      dispatch(order, &out);
    },
    [&](Cancel c) {
      cancel(c, &out);
    },
  }, request);
}

const std::unordered_map<OrderId, RestingOrder>& OrderBook::orders() const {
  return orders_;
}

OrderId OrderBook::gen_id() {
  next_id_ += OrderId(1);
  return next_id_ - OrderId(1);
}

// Responsible only for emission of `Rejected` or `Cancelled`
void OrderBook::cancel(Cancel c, std::vector<Event>* out) {
  auto it = orders_.find(c.id);

  if (it == orders_.end()) {
    emit(out, Rejected { c, Rejected::Reason::OrderIdNotFound });
    return;
  }

  RestingOrder order = it->second;

  switch (order.side) {
    case Side::Ask: {
      auto itp = sell_orders_.find(order.price);
      TPZ_ASSERT(itp != sell_orders_.end()); // this would be an internal error

      std::list<OrderId>* ids = &itp->second;
      auto ito = std::find(ids->begin(), ids->end(), order.id);
      TPZ_ASSERT(ito != ids->end()); // this would be an internal error

      ids->erase(ito);
      break;
    }
    case Side::Bid: {
      auto itp = buy_orders_.find(order.price);
      TPZ_ASSERT(itp != buy_orders_.end()); // this would be an internal error

      std::list<OrderId>* ids = &itp->second;
      auto ito = std::find(ids->begin(), ids->end(), order.id);
      TPZ_ASSERT(ito != ids->end()); // this would be an internal error

      ids->erase(ito);
      break;
    }
  }

  orders_.erase(it);
  
  emit(out, Cancelled { order.id });
}

void OrderBook::emit(std::vector<Event>* out, Event e) {
  if (out) out->push_back(e);
}

} // namespace tpz::eng
