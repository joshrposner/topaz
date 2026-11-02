#include <topaz/engine/order_book.hpp>

#include <utility>

namespace tpz::eng {

OrderBook::OrderBook() noexcept : next_id_(OrderId(1)) {}

void OrderBook::submit(Request request, std::vector<Event>& out) {
  std::visit(VisitHelper {
    [&](NewLimit order) {
      match(order, out);
    },
    [&](NewMarket order) {
      match(order, out);
    },
    [&](Cancel c) {
      cancel(c.id, out);
    },
  }, request);
}

OrderId OrderBook::gen_id() {
  next_id_ += OrderId(1);
  return next_id_ - OrderId(1);
}

void OrderBook::match(NewLimit new_order, std::vector<Event>& out) {
  OrderId new_id = gen_id();
  out.push_back(Accepted { new_id, new_order });
  if (new_order.side == Side::Ask) {
    // TODO: support FillOrKill orders
    // Look at orders at each price level, descending
    for (auto it_p = buy_orders_.begin(); it_p != buy_orders_.end();) {
      
      Price price = it_p->first;
      std::list<OrderId>& orders_at_price = it_p->second;

      if (price < new_order.price) break;
      
      // Look at the orders at this price level, ordered by recency (FIFO)
      for (auto it_o = orders_at_price.begin(); it_o != orders_at_price.end();) {
        RestingOrder resting_order = orders_.at(*it_o);
        if (resting_order.qty >= new_order.qty) {
          // order filled, resting order partially filled
          out.push_back(Filled { new_id, Side::Ask, price, new_order.qty });
          out.push_back(Filled { resting_order.id, Side::Bid, price, new_order.qty });
          out.push_back(Fulfilled { new_id });
          resting_order.qty -= new_order.qty;
          break;
        }
        else {
          // order partially filled, resting order filled
          out.push_back(Filled { new_id, Side::Bid, price, resting_order.qty });
          out.push_back(Filled { resting_order.id, Side::Bid, price, resting_order.qty });
          out.push_back(Fulfilled { resting_order.id });
          new_order.qty -= resting_order.qty;
          it_o = orders_at_price.erase(orders_at_price.begin());
        }
      }
      
      // at this point, we've looked at all orders at this price.
      // there may or may not be resting orders left at this price.
      // the new order may or may not have been filled.
      if (orders_at_price.empty()) it_p = buy_orders_.erase(it_p);
      else it_p++;
      
      if (new_order.qty == Qty(0)) { // the new order was fulfilled
        break;
      }
    }

    // at this point, we've looked at all matching resting orders.
    // the new order may or may not have been filled.
    if (new_order.qty > Qty(0)) {
      orders_[new_id] = RestingOrder {
        new_id,
        new_order.side,
        new_order.price,
        new_order.qty
      };
      sell_orders_[new_order.price].push_back(new_id);
    }
  }
  else if (new_order.side == Side::Bid) {
    // TODO:
  }
  else {
    std::unreachable();
  }
}

void OrderBook::match(NewMarket order, std::vector<Event>& out) {
  
}

void OrderBook::cancel(OrderId order, std::vector<Event>& out) {
  //  
}

} // namespace tpz::eng
