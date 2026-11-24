# Phase 2: remaining work

This expands the unchecked Phase 2 items in [`docs/roadmap.md`](../docs/roadmap.md). It assumes you know nothing about the terms used there.

## Why Phase 2 exists

In Phase 5 you'll replace `std::map`, `std::list`, and `std::unordered_map` with hand-written data structures. Those rewrites are where bugs come from: an off-by-one in a free list, a level that doesn't get unlinked, a cancel that removes the wrong order. Your scenario tests catch bugs in situations you thought of. Phase 2 adds tests that catch bugs in situations you didn't think of. They do that in three ways:

1. **Lots of random input**, so the book hits combinations nobody would write by hand.
2. **Invariant checks**, so you can tell the book is broken without knowing the expected output.
3. **A trusted second implementation**, so you can tell the output is wrong by comparing against it.

Everything below builds toward those three things.

---

## Concepts (the "Learn" items)

### Golden / snapshot tests

A **golden test** feeds a fixed input to the code and compares the output against an expected output saved in a file. The saved file is the "golden" copy. A **snapshot test** is the same idea. The name refers to how the expected output is usually produced: run the code once, save whatever it produced, and from then on fail if the output ever changes.

You already have half of this. Each file in `tests/scenarios/` is a recorded input with its expected events. The difference:

- **Your scenarios**: you wrote the expected output by hand, from your understanding of the rules. Short, readable, one rule per file.
- **A snapshot**: the program wrote the expected output. It can be huge (10,000 requests and their events). Nobody reads it line by line. It doesn't say the output is *correct*, only that it hasn't *changed*.

Why that's useful: before you touch the data structures in Phase 5, you record a snapshot from the Phase 1 book, which your scenarios say is correct. After every swap, the new book must reproduce that snapshot exactly. Any difference means behavior changed somewhere, even in a case no scenario covers.

When a snapshot fails on purpose (you changed a rule deliberately), you regenerate it and review the diff. Snapshot tools usually have an "update" mode for this, e.g. an environment variable that makes the test overwrite the file instead of comparing.

### Differential testing

**Differential testing** runs two implementations of the same thing on the same input and checks that they produce identical output. You don't need to know what the right answer is, only that both agree. If they disagree, at least one is wrong, and you have the exact input that shows it.

For Topaz the two implementations are:

- the **reference implementation**: the Phase 1 naive book. Slow, simple, and covered by your scenarios, so you trust it.
- the **implementation under test**: the optimized Phase 5 book.

You feed both the same long random stream of requests and compare, after every request:

- the events emitted (`std::vector<Event>`),
- `levels(Side::Bid)` and `levels(Side::Ask)`,
- `orders()`.

Comparing after every request rather than only at the end matters. A mismatch is reported at the first request where the books diverge, not 5,000 requests later.

Snapshots vs differential, briefly: a snapshot compares against output recorded in the past, and a differential test compares against output computed now. The snapshot is cheaper and simpler. The differential test lets you run millions of fresh random requests (any seed) instead of only the ones you recorded.

---

## Build items

Suggested order: invariant checker, then the generator, then a random test that combines them, then the snapshot, then the reference/differential setup. Each step is useful on its own, and each one makes the next easier.

### 1. Book invariant checker

An **invariant** is a property that must be true of the book between any two requests, no matter what happened before. A checker is a function that inspects the book and fails the test if any invariant is violated. Its value is that it needs no expected output: you can run it after every request of a random stream.

The roadmap lists these:

- **Best bid < best ask.** If both sides are non-empty, the highest bid is strictly below the lowest ask. If they were equal or crossed, those orders should have matched instead of resting. (A crossed book is the classic symptom of a broken match loop.)
- **No empty levels.** Every price level that exists has at least one order in it. Your Phase 1 rule is that emptied levels are removed. A leftover empty level would make the best bid/ask wrong.
- **Quantities positive.** Every resting order has qty > 0. A fully filled order should have been removed, and a negative qty means arithmetic went wrong.
- **Per-level totals add up.** For each level, the qty reported by `levels()` equals the sum of the qtys of the resting orders at that price on that side.

Other invariants worth adding:

- Levels are strictly sorted best-first (bids descending, asks ascending), with no duplicate prices.
- Every resting order's side and price match the level it's in.
- Every order id stored in a level exists in the id → order map, and every order in the map appears in exactly one level. (This catches an order that was removed from one structure but not the other, which is the most likely Phase 5 bug.)

**Design question you need to answer: what can the checker see?** `OrderBook`'s public API is `orders()` and `levels()`. Some invariants can be checked from outside with only those (totals add up, best bid < best ask, qty positive, sorted levels). Others need the internals: "no empty levels" can only be seen through `levels()` as a level with qty 0, and "every id in a level list exists in the map" can't be seen from outside at all. Options:

- **Public API only.** The checker lives in `tests/common/` and works on any book type that has `orders()` and `levels()`, which is good for Phase 5. But it can't check the internal cross-references.
- **A member function** like `OrderBook::check_invariants()`. It sees everything, but test-only code ends up in the library, and every new implementation has to write its own.
- **A `friend`** test helper. It sees everything without being part of the public API, but it's tied to one class's private members.

There's no single right choice. Pick one and record it in `docs/decisions.md`.

**Where:** `tests/common/invariants.hpp`. Add the `? invariants` check to the scenario runner too if you want, or just call the checker after every request in `run()`.

**Done when:** the checker runs after every request in every scenario file and all of them still pass. Then break the book on purpose (e.g. comment out the line that erases an empty level) and confirm the checker catches it. A checker you've never seen fail isn't proven to work.

### 2. Random order-flow generator with a fixed seed

**Order flow** is the stream of requests arriving at the exchange. The generator produces a `std::vector<Request>` of realistic-looking random requests.

**Seeds and reproducibility.** A pseudo-random number generator (PRNG) produces a sequence that looks random but is fully determined by its starting value, the **seed**. Same seed, same sequence, every run. That's what makes random tests usable: when seed 1234 fails, you can rerun seed 1234 and get the exact same failure to debug. Always print the seed in the failure message (e.g. via `SCOPED_TRACE`).

**A cross-compiler trap.** `std::mt19937` and `std::mt19937_64` produce exactly the same numbers on every compiler, because the standard specifies the algorithm. But `std::uniform_int_distribution` and the other distributions do **not**: GCC, Clang, and MSVC each implement them differently, so the same seed gives different values. That doesn't matter for a random test that just checks invariants, but it breaks snapshot files (step 4), which would then differ per compiler. Since Topaz must build on all three, map raw PRNG output to ranges yourself (or use a tiny self-written PRNG like splitmix64) instead of using `<random>` distributions. Record whichever you choose in `docs/decisions.md`.

**What "realistic" means.** Uniformly random prices from 1 to 1,000,000 would almost never cross, so nothing would ever match. Make the flow look like a real market:

- **Prices cluster near the mid.** Pick a mid price (e.g. 10,000) and choose prices within a few ticks of it, more often close than far. That way new orders often cross the spread and trade, and levels get created and emptied all the time.
- **A mix of request types**, controlled by weights: e.g. mostly GTC limits, a good share of cancels, some IOC/FOK limits, a few market orders.
- **Cancels mostly target live orders.** A cancel of a random number almost always hits an unknown id, which only tests the rejection path. Have the generator remember which ids it has handed out (ids are 1, 2, 3, … in accept order) and usually pick one of those. Some will already be filled or cancelled, which is good: those test "cancel a filled order is rejected".
  - Subtlety: the generator can't know which orders were accepted without asking a book. Either let it assume every order it generated got the next id (close enough, and keeps the generator independent of any book), or let it look at the book's results as it goes. The first option keeps the input stream a plain list you can feed to two books identically, which you want for step 5.
- **Some invalid input**, e.g. occasional zero qty, so rejection paths are exercised too.
- **Quantities** in a small range (1–50 or so), so orders partially fill against each other often.

Make the knobs (seed, count, mid, price spread, type weights) a config struct so tests and later the Phase 4 benchmark workload generator can reuse it.

**Bonus: dump to scenario format.** You already have `repr()` and the scenario text format. If the generator's output can be written as a `.txt` scenario (`> BUY 10 @ 10003` lines), a failing random run can be saved straight into `tests/scenarios/` and replayed or trimmed by hand.

**Where:** `tests/common/generator.hpp`, with a `tests/common/generator_test.cpp` that checks the generator itself: same seed gives the same sequence, different seeds give different sequences, all generated prices fall in the configured range.

**Done when:** you can generate, say, 10,000 requests from a seed and get the identical list every run.

### 3. Random test: generator + invariants

Not a separate roadmap item, but it's the payoff of steps 1 and 2 and it works today, with only one book:

- For a handful of seeds, generate a few thousand requests, submit each to an `OrderBook`, and run the invariant checker after every request.
- Also count how many of each event type came out. If a run produced zero `Filled` events, the generator isn't producing realistic flow, and the test is passing without testing much. Assert minimum counts, or at least print them while tuning.

This is a basic form of **property-based testing**: instead of checking specific outputs, you check properties that must hold for any input.

**Where:** `tests/random_test.cpp` (core engine tests sit directly in `tests/`). Remember to add it to `tests/CMakeLists.txt`.

### 4. Golden snapshot of the Phase 1 book

- Generate a long request stream from a fixed seed and config.
- Run it through the current `OrderBook` and write every request and its events (via `repr()`) to a file, e.g. `tests/golden/seed_1.txt`. This is the golden file. Commit it.
- The test regenerates the stream, runs it, and compares against the file line by line, reporting the first differing line.
- Add an update mode (e.g. an environment variable `TPZ_UPDATE_GOLDEN=1`) that rewrites the file instead of comparing, for when you change behavior on purpose. Review the diff in git before committing.

Things to get right:

- The output must be deterministic. Anything that produces events or output by iterating an `std::unordered_map` (like `orders()`) has an unspecified order that can differ between compilers and standard libraries, which would make the snapshot flaky. `std::map` and `std::list` iterate in a defined order.
- The file depends on the generator. If you change the generator, the golden file changes too, even if the book didn't. Changing both in one commit makes a failure ambiguous, so change one at a time.
- Line endings: open files consistently so Windows doesn't turn `\n` into `\r\n` and break the comparison.

### 5. Keep the naive book as the reference implementation

Right now there's only one book, so this item is about preparing. In Phase 5, `OrderBook` will change. If the Phase 1 version is gone by then, you have nothing to compare against except the snapshot.

Decisions to make (and record in `docs/decisions.md`):

- **Where the reference lives.** Options: copy the current book into the test tree (e.g. `tests/common/reference_book.hpp`, class `ReferenceBook`) and never optimize it; or keep it in the library under a different name. Test-tree only keeps it out of the shipped library. Library-side lets the GUI or benchmarks use it too, e.g. to benchmark naive vs optimized.
- **How tests talk to "any book".** The differential harness needs to accept both types. Since the project avoids virtual dispatch, a template parameter constrained by a C++20 **concept** (e.g. a `BookLike` concept requiring `submit(Request, std::vector<Event>&)`, `orders()`, and `levels(Side)`) is the natural fit. The invariant checker and scenario runner can take the same concept, so every scenario also runs against every implementation for free. That's typed tests, see GoogleTest's `TYPED_TEST`.
- **What gets compared.** Events per request and `levels()` per side are straightforward. `orders()` returns `std::unordered_map` today. If Phase 5 changes that return type, comparing needs a common form (e.g. a sorted list of `RestingOrder`).

**Build the differential harness now, before you have a second book**, so it's ready and proven in Phase 5:

- Write `run_differential<A, B>(requests)`, which submits each request to both books and compares everything after each one, reporting the request index and seed on the first mismatch.
- Test it with `OrderBook` against `OrderBook`. It should trivially pass.
- Then test it against a deliberately broken copy (e.g. one that matches LIFO instead of FIFO) and confirm it reports the mismatch at the right request. As with the invariant checker, you want to see it fail once.

**Where:** `tests/common/differential.hpp`, and `tests/differential_test.cpp`.

---

## Checklist

- [ ] Read up on golden/snapshot and differential testing (above is enough to start)
- [ ] Invariant checker in `tests/common/`, run after every scenario request, seen catching a deliberate bug
- [ ] Decision recorded: what the invariant checker can see
- [ ] Seeded generator with configurable mix, clustered prices, and mostly-live cancels, plus tests for the generator itself
- [ ] Decision recorded: RNG choice (cross-compiler reproducibility)
- [ ] Random test: several seeds × thousands of requests, invariants after each, event-type counts sanity-checked
- [ ] Golden file recorded from the Phase 1 book, with an update mode
- [ ] Reference book's home and the "any book" concept decided and recorded
- [ ] Differential harness, seen passing (book vs itself) and failing (book vs broken copy)
- [ ] Check off the Phase 2 items in `docs/roadmap.md`
