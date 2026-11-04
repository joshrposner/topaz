#pragma once

#include <cstdint>
#include <functional>
#include <limits>
#include <variant>

namespace tpz {

/// @brief Integer wrapper that makes each @p Tag a distinct,
/// non-interchangeable type.
///
/// Prevents mixing up values that share a representation, such as passing a
/// price where a quantity is expected. Construction from a raw integer is
/// explicit, so every value is labeled at the call site: `Price(10125)`, not
/// `10125`. Supports comparison only; there is no arithmetic yet.
///
/// All members are `constexpr`, so values can be used in constant expressions
/// and `static_assert`.
///
/// @tparam Tag Empty struct that only distinguishes one StrongInt type from
/// another.
template <typename Tag>
class StrongInt {
 public:
  /// @brief Underlying integer representation.
  using Rep = std::int64_t;

  /// @brief Creates a value of zero.
  constexpr StrongInt() : val_(0) {};

  /// @brief Wraps the raw integer @p val.
  constexpr explicit StrongInt(Rep val) : val_(val) {}

  /// @brief Adds @p rhs to the underlying value.
  constexpr StrongInt& operator+=(StrongInt rhs) {
    val_ += rhs.val_;
    return *this;
  }
  /// @brief Subtracts @p rhs from the underlying value.
  constexpr StrongInt& operator-=(StrongInt rhs) {
    val_ -= rhs.val_;
    return *this;
  }

  /// @brief Sum of the underlying values.
  friend constexpr StrongInt operator+(StrongInt lhs, StrongInt rhs) {
    return lhs += rhs;
  }
  /// @brief Difference of the underlying values.
  friend constexpr StrongInt operator-(StrongInt lhs, StrongInt rhs) {
    return lhs -= rhs;
  }

  /// @brief Returns the underlying integer.
  [[nodiscard]] constexpr Rep val() const { return val_; }

  /// @brief Compares by underlying value. Also provides `==` and `!=`.
  friend constexpr auto operator<=>(StrongInt, StrongInt) = default;

 private:
  Rep val_;
};

struct PriceTag {};
struct QtyTag {};
struct OrderIdTag {};

/// @brief Price in integer ticks. Never floating point.
using Price = StrongInt<PriceTag>;
/// @brief Order quantity in whole units.
using Qty = StrongInt<QtyTag>;
/// @brief Client-assigned order identifier. A Cancel uses it to find its order.
using OrderId = StrongInt<OrderIdTag>;

/// @brief Side of the book an order belongs to.
enum class Side {
  Bid,  ///< Buy order.
  Ask,  ///< Sell order.
};

/// @brief Time in force for a limit order: what happens to any part of it that
/// does not fill on
///        arrival.
enum class Tif {
  /// Fill what is possible now; the remainder rests on the book until filled or
  /// cancelled.
  GoodTilCancel,
  /// Fill what is possible now; cancel the remainder. Never rests on the book.
  ImmediateOrCancel,
  /// Fill the entire quantity now, or cancel the whole order with no fills.
  FillOrKill,
};

/// @brief Time in force for a market order. Same meanings as the matching Tif
/// values.
///
/// There is no GoodTilCancel: a market order has no price, so its remainder
/// cannot rest on the book. Separate from Tif so that combination cannot be
/// represented.
///
/// @note The underlying values differ from Tif's, so convert with a `switch`,
/// not a cast.
enum class MarketTif {
  /// Fill what is possible now; cancel the remainder. Never rests on the book.
  ImmediateOrCancel,
  /// Fill the entire quantity now, or cancel the whole order with no fills.
  FillOrKill,
};

/// @brief Request to submit a limit order.
///
/// Matches only at @c price or better: at or below @c price for a bid, at or
/// above it for an ask. What happens to unfilled quantity depends on @c tif.
struct NewLimit {
  Side side;
  Price price;  ///< Worst acceptable price.
  Qty qty;
  Tif tif;

  friend bool operator==(const NewLimit&, const NewLimit&) = default;
};

/// @brief Request to submit a market order.
///
/// Matches at any price, best price first, until filled or the opposite side
/// runs out. Never rests on the book; what happens to unfilled quantity depends
/// on @c tif.
struct NewMarket {
  Side side;
  Qty qty;
  MarketTif tif;

  friend bool operator==(const NewMarket&, const NewMarket&) = default;
};

/// @brief Request to cancel the resting order with @c id.
struct Cancel {
  OrderId id;

  friend bool operator==(const Cancel&, const Cancel&) = default;
};

/// @brief Any request the engine accepts.
using Request = std::variant<NewLimit, NewMarket, Cancel>;

/// @brief Order waiting on the book to be matched.
struct RestingOrder {
  OrderId id;
  Side side;
  Price price;
  Qty qty;  ///< Remaining unfilled quantity, which can be less than the
            ///< original order's.

  friend bool operator==(const RestingOrder&, const RestingOrder&) = default;
};

/// @brief One price level of one side of the book, as in L2 market data.
struct Level {
  Price price;
  Qty qty;  ///< Total remaining qty of the orders resting at @c price.

  friend bool operator==(const Level&, const Level&) = default;
};

/// @brief Helper utility to match variants
template <class... Ts>
struct VisitHelper : Ts... {
  using Ts::operator()...;
};
template <class... Ts>
VisitHelper(Ts...) -> VisitHelper<Ts...>;

}  // namespace tpz

template <typename Tag>
struct std::hash<tpz::StrongInt<Tag>> {
  std::size_t operator()(tpz::StrongInt<Tag> v) const noexcept {
    return std::hash<typename tpz::StrongInt<Tag>::Rep>{}(v.val());
  }
};

template <typename Tag>
struct std::numeric_limits<tpz::StrongInt<Tag>>
    : std::numeric_limits<typename tpz::StrongInt<Tag>::Rep> {
  using T = tpz::StrongInt<Tag>;
  using R = std::numeric_limits<typename T::Rep>;
  static constexpr T min() noexcept { return T(R::min()); }
  static constexpr T max() noexcept { return T(R::max()); }
  static constexpr T lowest() noexcept { return T(R::lowest()); }
};
