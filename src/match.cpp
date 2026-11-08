#include <limits>
#include <print>
#include <topaz/defs.hpp>
#include <topaz/order_book.hpp>
#include <utility>

namespace tpz {

// Responsible only for emission of `Rejected`/`Accepted` and `Canceled` for
// IOC/FOK
void OrderBook::dispatch(NewLimit order, std::vector<Event>* out) {
  if (order.qty <= Qty(0)) {
    emit(out, Rejected(order, Rejected::Reason::InvalidQty));
    return;
  }

  OrderId id = gen_id();
  emit(out, Accepted(id, order));

  switch (order.tif) {
    case Tif::GoodTilCancel: {
      Qty rem = sweep(id, order.side, order.price, order.qty, out);
      rest(id, order.side, order.price, rem);
      break;
    }
    case Tif::ImmediateOrCancel: {
      Qty rem = sweep(id, order.side, order.price, order.qty, out);
      if (rem > Qty(0)) emit(out, Canceled(id));
      break;
    }
    case Tif::FillOrKill: {
      if (!can_fulfill(order.side, order.price, order.qty)) {
        emit(out, Canceled(id));
        break;
      }
      sweep(id, order.side, order.price, order.qty, out);
      break;
    }
  }
}

// Responsible only for emission of `Rejected`/`Accepted` and `Canceled` for
// IOC/FOK
void OrderBook::dispatch(NewMarket order, std::vector<Event>* out) {
  if (order.qty <= Qty(0)) {
    emit(out, Rejected(order, Rejected::Reason::InvalidQty));
    return;
  }

  OrderId id = gen_id();
  emit(out, Accepted(id, order));

  Price price_max = std::numeric_limits<Price>::max();
  Price price_min = std::numeric_limits<Price>::min();
  Price sentinel_price = (order.side == Side::Ask ? price_min : price_max);

  switch (order.tif) {
    case MarketTif::ImmediateOrCancel: {
      Qty rem = sweep(id, order.side, sentinel_price, order.qty, out);
      if (rem > Qty(0)) emit(out, Canceled(id));
      break;
    }
    case MarketTif::FillOrKill: {
      if (!can_fulfill(order.side, sentinel_price, order.qty)) {
        emit(out, Canceled(id));
        break;
      }
      sweep(id, order.side, sentinel_price, order.qty, out);
      break;
    }
  }
}

// No emissions.
void OrderBook::rest(OrderId id, Side side, Price price, Qty rem) {
  if (rem <= Qty(0)) return;

  RestingOrder rest(id, side, price, rem);
  orders_[id] = rest;
  switch (side) {
    case Side::Ask:
      sell_orders_[price].push_back(id);
      break;
    case Side::Bid:
      buy_orders_[price].push_back(id);
      break;
  }
}

namespace {
// Helper to compare a price to the limit.
bool crosses(Side side, Price level_price, Price limit) {
  switch (side) {
    case Side::Ask:
      return level_price < limit;
    case Side::Bid:
      return level_price > limit;
  }
  std::unreachable();
}
}  // namespace

namespace {
// Same as `OrderBook::emit`, which the free helpers below can't call because
// it's private.
void emit_event(std::vector<Event>* out, Event e) {
  if (out) out->push_back(e);
}

// Per-side implementation of `OrderBook::can_fulfill`. `Book` is the opposite
// side's map. No emissions.
template <class Book>
bool can_fulfill_book(Book& book,
                      std::unordered_map<OrderId, RestingOrder>& orders,
                      Side side, Price limit, Qty qty) {
  for (auto [price, ids] : book) {
    if (crosses(side, price, limit)) break;
    for (OrderId id : ids) {
      auto it = orders.find(id);
      TPZ_ASSERT(it != orders.end());  // this would be an internal error
      qty -= it->second.qty;
      if (qty <= Qty(0)) break;
    }
    if (qty <= Qty(0)) break;
  }
  return qty <= Qty(0);
}

// Per-side implementation of `OrderBook::sweep`. `Book` is the opposite side's
// map. Responsible only for emission of `Filled`/`Fulfilled`
template <class Book>
Qty sweep_book(Book& book, std::unordered_map<OrderId, RestingOrder>& orders,
               OrderId id, Side side, Price limit, Qty qty,
               std::vector<Event>* out) {
  for (auto itp = book.begin(); itp != book.end();) {
    Price price = itp->first;
    std::list<OrderId>& ids = itp->second;
    if (crosses(side, price, limit)) break;

    for (auto iti = ids.begin(); iti != ids.end();) {
      auto ito = orders.find(*iti);
      TPZ_ASSERT(ito != orders.end());  // this would be an internal error

      RestingOrder& order = ito->second;
      if (order.qty >= qty) {
        // sufficient quantity, incoming order fulfilled, resting order
        // partially filled (or resting order fulfilled if equal qtys)
        emit_event(out, Filled(id, side, price, qty));
        emit_event(out, Filled(order.id, order.side, price, qty));
        order.qty -= qty;
        qty = Qty(0);
        emit_event(out, Fulfilled(id));
        if (order.qty == Qty(0)) {
          emit_event(out, Fulfilled(order.id));
          orders.erase(ito);
          iti = ids.erase(iti);
        }
        break;
      } else if (qty > order.qty) {
        // insufficient quantity, incoming order partially filled, resting order
        // fulfilled
        emit_event(out, Filled(id, side, price, order.qty));
        emit_event(out, Filled(order.id, order.side, price, order.qty));
        qty -= order.qty;
        order.qty = Qty(0);
        emit_event(out, Fulfilled(order.id));
        orders.erase(ito);
        iti = ids.erase(iti);
      }
    }
    if (ids.empty())
      itp = book.erase(itp);
    else
      itp++;

    if (qty == Qty(0)) break;
  }
  return qty;
}
}  // namespace

// No emissions.
bool OrderBook::can_fulfill(Side side, Price limit, Qty qty) {
  switch (side) {
    case Side::Ask:
      return can_fulfill_book(buy_orders_, orders_, side, limit, qty);
    case Side::Bid:
      return can_fulfill_book(sell_orders_, orders_, side, limit, qty);
  }
  std::unreachable();
}

// Responsible only for emission of `Filled`/`Fulfilled`
Qty OrderBook::sweep(OrderId id, Side side, Price limit, Qty qty,
                     std::vector<Event>* out) {
  switch (side) {
    case Side::Ask:
      return sweep_book(buy_orders_, orders_, id, side, limit, qty, out);
    case Side::Bid:
      return sweep_book(sell_orders_, orders_, id, side, limit, qty, out);
  }
  std::unreachable();
}

}  // namespace tpz
