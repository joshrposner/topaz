<p align="center">
  <img src="logo.png" alt="Topaz logo">
</p>

<p align="center">
  <a href="https://github.com/jopo86/topaz/actions/workflows/build.yml">
    <img src="https://github.com/jopo86/topaz/actions/workflows/build.yml/badge.svg?branch=release" alt="Build status">
  </a>
</p>

# Topaz v0.1
(Eventual) Low-latency C++ order book and matching engine, with custom containers and networking

## Features
- MVP `OrderBook` with matching engine
- Market and limit orders with GTC/IOC/FOK time-in-forces

## Limitations
- Engine uses naive containers and is not optimized yet
- No networking or GUI yet

## Build
From a fresh clone, configure, build, and test in release:
```
cmake --workflow --preset test
```
To start over, delete `build/<preset>/` (or all of `build/`).

## Example
```cpp
#include <topaz/order_book.hpp>
using namespace tpz;

OrderBook book;
std::vector<Event> out;

book.submit(NewLimit(Side::Ask, Price(10125), Qty(10), Tif::GoodTilCancel), out);
// Accepted{#1}  (rests: ask 10 @ $101.25)

book.submit(NewLimit(Side::Bid, Price(10126), Qty(15), Tif::GoodTilCancel), out);
// Accepted{#2}, Filled{#2 Bid 10 @ $101.25}, Filled{#1 Ask 10 @ $101.25}, Fulfilled{#1}
// The trade happens at the resting price; the remaining 5 rest as the best bid:
// book.levels(Side::Bid) == {Level(Price(10126), Qty(5))}
```
Prices and quantities are integers (prices in ticks). Comments assume tick size is 0.01.
Events are appended to `out` in order.

## Future of Topaz
See the [roadmap](docs/roadmap.md).
