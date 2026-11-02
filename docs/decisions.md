# Design decisions

## `tpz::dsa::List`

### Forward-owning nodes (2026-10-28)

**Decision:** Each node owns the next one. `head_` and `next_` are `std::unique_ptr`; `tail_` and `prev_` are raw, non-owning pointers.

**Reasoning:** A doubly linked list has two links into each node, but only one of them can own it. Owning in the forward direction keeps memory management in the types: no manual `delete`, and moves come almost for free.

**Alternatives considered:**
- *All `unique_ptr`*: impossible, because every node would have two unique owners (the previous node's `next` and the next node's `prev`). In practice that means double frees, or not compiling at all.
- *All raw pointers*: works, but every insert, erase, move, and the destructor need hand-written `new`/`delete`, which makes leaks and double frees easy to introduce.

### Iterative destruction (2026-10-29)

**Decision:** `clear()` frees nodes one at a time in a loop. The destructor and move assignment both call it.

**Reasoning:** With forward ownership, letting `head_` go out of scope destroys the chain recursively: each node's `next_` destructor frees the next node. That is one stack frame per element, so a long list overflows the stack. The loop cuts each `next_` link before the node is freed, so stack depth stays constant.

**Alternatives considered:**
- *Recursive (default) destruction*: no code needed, but stack depth grows with the list length. `LongListDestroysWithoutStackOverflow` tests that we avoid this.

## Core Shared Types

### Shared types live in `tpz`, in `topaz/types.hpp` (2026-10-30)

**Decision:** `Price`, `Qty`, `OrderId`, `Side`, and the request types live directly in namespace `tpz`, in `include/topaz/types.hpp`.

**Reasoning:** These types are the vocabulary of the whole system, not engine internals. `tpz::net` needs them to encode and decode order entry and market data messages, and the GUI needs them to display and submit orders. Keeping them out of `tpz::eng` means neither has to depend on the engine just to name a price.

**Alternatives considered:**
- *`tpz::eng` in `topaz/engine/types.hpp`*: also workable, since `net` depending on `eng` is the allowed direction. Rejected because anything that needs a `Price` would then depend on the engine.
- *`defs.hpp` / `common.hpp`*: vague names like these tend to collect unrelated macros and helpers over time. Headers are named after what they contain, like `assert.hpp`.

### `StrongInt` has an `explicit` constructor (2026-10-30)

**Decision:** `StrongInt`'s constructor from `Rep` is `explicit`, so raw integers don't convert implicitly to `Price`, `Qty`, or `OrderId`.

**Reasoning:** The point of strong types is to make mix-ups fail to compile. With an implicit constructor, a `Price` still can't be passed as a `Qty`, but raw integers convert to any strong type, so `NewLimit{1, Side::Bid, 100, 10125}` with price and qty swapped would compile. With `explicit`, every value is labeled at the call site: `NewLimit{OrderId{1}, Side::Bid, Price{10125}, Qty{100}}`.

**Alternatives considered:**
- *Implicit constructor*: shorter tests and call sites, but swapped integer literals compile without complaint. Rejected because that's exactly the kind of bug these types exist to catch.

### Requests are a `std::variant`; the book stores `RestingOrder` (2026-10-30)

**Decision:** Incoming requests are `Request = std::variant<NewLimit, NewMarket, Cancel>`. The book stores a single concrete type, `RestingOrder`.

**Reasoning:** The set of request types is small and closed, which is what `std::variant` is for. `std::visit` doesn't compile if a case is unhandled, so adding a request type (e.g. modify) makes the compiler point out every place that needs updating. Alternatives are stored inline (size of the largest alternative plus a discriminant), so there's no per-request heap allocation or virtual dispatch. That should be fast, but it hasn't been measured yet.

Only limit orders ever rest, because a market order trades immediately and any unfilled remainder is cancelled. So the variant is only for input. If the book stored a variant, every read of a resting order would have to visit something that can only be one type.

**Alternatives considered:**
- *Inheritance* (`NewLimit`, `NewMarket` derive from a base `Request`): suits an open, extensible set of types. Here it would put every request behind a pointer, which means a heap allocation per request plus virtual dispatch, all for a set of types we know in advance.
- *One struct with an order-type enum* (like FIX's `OrdType`, with `price` ignored for market orders): simple and fixed-size, but it allows invalid states like a market order with a price, and nothing forces every type to be handled.
- *Variant for stored orders too* (`Order = std::variant<MarketOrder, LimitOrder>` in the book): rejected because market orders never rest; see above.

### Market orders use `MarketTif`, which has no GTC (2026-10-30)

**Decision:** `NewMarket::tif` is a separate `MarketTif` enum with only `ImmediateOrCancel` and `FillOrKill`. `NewLimit` keeps the full `Tif`.

**Reasoning:** A market order has no price, so its remainder can't rest on the book, and market + GTC is meaningless. Making the field's type unable to hold GTC removes that case entirely: the engine has nothing to check, assert, or reject, and matching code can't forget to handle it. It's the same reasoning as rejecting the single-struct request in favour of a variant.

The wire decoder still has to reject a GTC value in a market order message, but that's ordinary input validation it already does for any out-of-range field.

`MarketTif` and `Tif` share value names but not underlying values, so code that needs one TIF type converts with a `switch`, not a cast.

**Alternatives considered:**
- *Precondition + `TPZ_ASSERT`*: makes the engine's contract explicit, but `TPZ_ASSERT` becomes `[[assume]]` in Release, so a violation is undefined behaviour rather than a crash. Since requests will come from clients over `tpz::net`, this only works if every request source validates first, and nothing in the types enforces that.
- *Runtime reject in the engine*: safe and cheap (one predictable branch per market order), but it puts input validation into matching code and still leaves an invalid state representable.
- *Treat market + GTC as IOC*: silently changes what the client asked for. Rejected because it hides client bugs.

## `tpz::eng::OrderBook`

### Matching is a private member of `OrderBook` (2026-10-31)

**Decision:** Matching is implemented as private helper(s) on `OrderBook`, called from `submit()`. There is no separate `Matcher` type.

**Reasoning:** Matching changes the book's own state: it walks the opposite side's price levels, reduces or removes resting orders, and emits events. A separate matcher would need references to `orders_`, `buy_orders_`, `sell_orders_`, and the output buffer, so it would just be the book's internals moved somewhere else, without any real decoupling. Keeping matching as a member keeps those invariants inside one class, and the containers stay private.

There is also no need to swap matching rules. The book implements price-time priority, and that's the only algorithm planned. Phase 2's differential tests compare against the whole naive book, not a different matcher, so they don't need a swappable matcher either.

Matching is tested through the public `submit()` API and the events it emits, which tests behaviour rather than internal structure.

**Alternatives considered:**
- *`Matcher` with static methods, taking references to the book's state*: the same coupling as a member function, but the book's containers have to be exposed or passed around, so encapsulation is weaker and nothing is gained.
- *Pure `Matcher` that returns the new book state and events instead of mutating in place*: easier to reason about in isolation, but it means copying the price-level maps and order map on every submit. That's heap allocation and O(book size) work on the hot path, which goes against the "Hot path first" principle.
- *`Matcher` interface injected into `OrderBook`*: modular, but it adds a virtual call to every submit for flexibility we don't need. If swappable rules are ever needed (e.g. pro-rata matching, or self-trade prevention modes), a template parameter (`OrderBook<MatchPolicy>`) would provide them with no virtual dispatch. That can be added later without changing the public API.

### Events go into a caller-owned buffer (2026-11-01)

**Decision:** `OrderBook` writes its events into a buffer the caller provides: `void submit(Request request, std::vector<Event>& out)`. The book keeps no event state of its own, it isn't templated on an event sink, and its implementation lives in `src/engine/order_book.cpp`.

**Reasoning:** The engine stays plain data in, data out. Tests submit a request and compare `out` against the expected events with `EXPECT_EQ`, which is also what Phase 2's golden and differential tests need. Determinism is easy to see, because the output is just a sequence of values.

Because the caller owns the buffer, the caller decides how long it lives. It can clear and reuse one vector across calls, and once the vector's capacity has grown it stops allocating. The book has no buffer that grows when nobody drains it, and it never keeps events nobody asked for.

If events are ever produced outside a request (e.g. session-end expiry of day orders, or a book-level halt), the operation that produces them takes an `out` parameter too, e.g. `expire_day_orders(out)`. Every event still has an obvious cause: the call that produced it.

**Alternatives considered:**
- *Internal buffer drained with `pop_events()`*: also non-templated with no virtual dispatch, but the book owns a buffer that grows without limit unless someone drains it. `std::exchange(events_, {})` also throws away the buffer's capacity on every pop, so the book keeps allocating.
- *Template sink constrained by a concept* (`OrderBook<EventSink Sink>` calling `sink_.on(event)`): nothing is buffered, and the sink's code can be inlined straight into the matching loop. Rejected for now because the whole implementation has to live in the header, and the sink type becomes part of the book's type, so `OrderBook<GuiSink>` and `OrderBook<TestSink>` are different types. Its advantage is performance, which waits until Phase 5. It's the first thing to benchmark against there if writing to the buffer shows up in profiles.
- *Virtual `EventSink` interface*: keeps the implementation in a `.cpp` and consumer types out of the book's type, but adds an indirect, non-inlinable call to every event. That goes against the "avoid virtual dispatch on the hot path" principle and gives nothing the buffer doesn't.
- *`submit()` returns a `std::vector<Event>`*: the same model, but every call allocates a fresh vector, and there's no way to reuse one.

### The book assigns order ids; `Accepted` carries them (2026-11-01)

**Decision:** `NewLimit` and `NewMarket` have no id. Each `OrderBook` assigns ids to the new orders it accepts, counting up from 1 in submission order, and reports the id in `Accepted{id, request}`. Rejected orders don't use an id. Cancels refer to the book's id, and a successful cancel emits only `Cancelled{id}`, not `Accepted`.

**Reasoning:** With client-chosen ids, two orders could share an id, and the book would need a rule for it. Assigning ids in the book makes a duplicate impossible, the same way `MarketTif` makes market + GTC impossible. Real exchanges work the same way: the exchange assigns the order id, and the client's own id (FIX's ClOrdID) is mapped to it per session, which here is the job of `tpz::net`.

Ids come from a per-book counter, so the same input sequence always produces the same ids, and tests can predict them. Starting at 1 keeps a default-constructed `OrderId{}` (0) from naming a real order.

The id goes in `Accepted` rather than a separate event. Every new order gets both an acceptance and an id at the same moment, so two events would describe one fact, and consumers would have to pair them up. Cancels no longer emit `Accepted`, because `Cancelled` already confirms them and `Accepted`'s id would mean something different for a cancel.

**Alternatives considered:**
- *Duplicate ids are a precondition violation* (`TPZ_ASSERT`): `TPZ_ASSERT` becomes `[[assume]]` in Release, so a duplicate from a client would be undefined behaviour. Unlike a GTC flag on a market order, a gateway can't check uniqueness without keeping its own set of every id in use.
- *Reject duplicates with a new `Rejected::Reason`*: checking `orders_` only catches ids that are still resting. Catching an id reused after its order filled or was cancelled would mean remembering every id forever.
- *Separate `IdAssigned` event*: see above. It would always come paired with `Accepted`, and a stream with one but not the other would be possible.
- *`submit()` returns the id*: only the direct caller would see it. Event consumers (the GUI, market data, Phase 2's recorded event logs) need the id to make sense of later `Filled` and `Cancelled` events, so it has to be in the event stream.
- *A counter shared by every book* (a `static` member): ids would depend on how many orders other books in the process had seen, which breaks determinism and makes tests depend on the order they run in.
