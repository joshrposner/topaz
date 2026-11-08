# CLAUDE.md

Context for Claude Code (and humans) working in this repository.

## Project

**Topaz** is a low-latency limit order book and matching engine written in C++23. It is a portfolio/learning project focused on performance-oriented systems design: cache-friendly data structures, predictable latency, and clean, testable architecture.

The repo contains:

- **`tpz`**: the core engine — order book, matching logic, order types, and event output. Lives directly in the top-level namespace.
- **`tpz::dsa`**: purpose-built data structures used by the engine (written from scratch, no third-party containers on the hot path).
- **`tpz::net`**: networking layer for order entry and market data, derived from [Garnet](https://github.com/jopo86/garnet).
- **Example app**: a Dear ImGui GUI that visualizes the book and lets you submit orders.

## Status

<!-- Update this as the project moves. Keep it short. -->
Phase 1 complete, naive book implemented. Current focus: test infrastructure

Progress is tracked in [`docs/roadmap.md`](docs/roadmap.md). Check it to see which phase is in progress and pitch explanations to match. Earlier phases deliberately use naive standard containers, so don't suggest Phase 5 optimizations while Phases 1–4 are still open.

## Repository layout

```
CMakeLists.txt      root build (library target: topaz / topaz::topaz)
include/topaz/      public headers; core engine (tpz) directly here
    dsa/                tpz::dsa
    net/                tpz::net
src/                implementation, mirrors include/topaz/
    dsa/
    net/
tests/              GoogleTest unit tests (target: tpz_tests), mirrors include/topaz/
    CMakeLists.txt
    dsa/
    net/
    common/             shared test helpers/fixtures (when needed)
bench/              Google Benchmark benchmarks (target: tpz_bench), mirrors include/topaz/
    CMakeLists.txt
    dsa/
examples/gui/       Dear ImGui example app
docs/               design notes and decisions
```

Tests and benchmarks have no `src/` layer. Core engine tests and benchmarks sit directly next to their `CMakeLists.txt`; side-module ones go in the matching subdirectory (`tests/dsa/`).

## Build

```sh
# Configure + build (Release)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

# Run tests
ctest --test-dir build --output-on-failure

# Run benchmarks (Release builds only)
./build/bench/tpz_bench

# Debug build with sanitizers
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug -DTPZ_ENABLE_SANITIZERS=ON
cmake --build build-debug -j
```

CMake options (all prefixed `TPZ_`): `TPZ_BUILD_TESTS`, `TPZ_BUILD_BENCH`, `TPZ_BUILD_EXAMPLES` (default ON when top-level), `TPZ_ENABLE_SANITIZERS`, `TPZ_WARNINGS_AS_ERRORS` (default OFF).

- Build out-of-source in `build*/` (gitignored).
- Warnings and sanitizer flags live on the `tpz_options` interface target; link it to every first-party target, never to third-party code.
- Fetched dependencies are marked `SYSTEM` so strict warnings don't apply to their headers.
- `compile_commands.json` is generated in the build dir; symlink it to the repo root for clangd if needed.
- The project must build on GCC, Clang, and MSVC.

## Design principles

- **Hot path first.** Order insert, cancel, and match should avoid heap allocation, virtual dispatch, exceptions, and locks. Prefer preallocated pools, contiguous storage, and intrusive structures.
- **Determinism.** Given the same input sequence, the engine must produce the same output events. No reliance on wall-clock time or unordered iteration in matching logic.
- **Measure, don't guess.** Performance claims need a benchmark in `bench/`. Note results in `docs/` when a design choice is made for performance reasons.
- **Separation of concerns.** Matching logic knows nothing about networking or the GUI. The engine consumes order events and emits execution/book events.
- **Correctness before speed.** Every matching rule gets a unit test before it gets optimized.

## Conventions

### Language

- C++23 (`CMAKE_CXX_STANDARD 23`, required, extensions off). Compile cleanly with `-Wall -Wextra -Wpedantic -Wshadow -Wconversion` on GCC/Clang and `/W4` on MSVC.
- Use well-supported C++23 features freely: `std::expected` for error handling on the hot path, `std::byteswap` for wire encoding, `std::unreachable`, `std::to_underlying`, deducing `this`, `std::print` (outside latency-sensitive code).
- Before using a C++23 library feature with uneven support (e.g. `std::flat_map`, `std::start_lifetime_as`), confirm it works on all three compilers.
- Prices and quantities are integer types (fixed-point ticks), never floating point.
- Initialize with parentheses, including aggregates (C++20 parenthesized aggregate init): `Price(10125)`, `Level(Price(10126), Qty(5))`, `NewLimit(Side::Bid, Price(100), Qty(10), Tif::GoodTilCancel)`. Braces only where parentheses can't work:
  - container/`std::initializer_list` construction: `Levels{Level(...), Level(...)}`
  - empty initialization, to avoid the most vexing parse: `Level x{}`, not `Level x()`
  - designated initializers: `Level{.price = ..., .qty = ...}`
- Parentheses don't reject narrowing conversions, so give fields strong types or enums rather than raw integers where a mix-up is possible.
- Prefer `std::span`, `std::string_view`, and value types over raw pointers and ownership ambiguity.

### Naming (Rust-style)

- `snake_case` for functions, variables, namespaces, and file names (`order_book.hpp`).
- `PascalCase` for types and concepts.
- `SCREAMING_SNAKE_CASE` for constants.
- Trailing `_` for private members (`price_`), so accessors can use the plain name (`price()`).
- All macros are prefixed with `TPZ_` (`TPZ_ASSERT`, `TPZ_LIKELY`) to avoid collisions with constants and third-party headers.
- Test files are `<name>_test.cpp`; benchmark files are `<name>_bench.cpp`; benchmark functions are `bm_<what>`.

### Namespaces and includes

- The core engine lives directly in `tpz`; side modules get abbreviated sub-namespaces: `tpz::dsa`, `tpz::net`.
- Directory structure matches namespaces: `tpz::OrderBook` is in `<topaz/order_book.hpp>`, `tpz::dsa::List` is in `<topaz/dsa/list.hpp>`. A new sub-namespace gets a matching subdirectory in `include/topaz/`, `src/`, `tests/`, and `bench/`.
- No `using namespace` in headers.

### Formatting

- Formatting via `.clang-format` in the repo root; run it before committing.

## Dependencies

- **GoogleTest** (tests) and **Google Benchmark** (benchmarks), fetched via `FetchContent` at pinned release tags.
- **Dear ImGui** (example app only).
- The `topaz` library itself has no third-party dependencies. Don't add any without asking.

## Working with Claude

- This is a learning project. For `tpz::dsa` and core matching logic, prefer explaining approaches, trade-offs, and reviewing my code over writing full implementations unless I ask for one.
- When adding a source file, also add it to the relevant `CMakeLists.txt` (sources are listed explicitly, no globbing).
- New behavior gets a test in `tests/<component>/`; performance-motivated changes get a benchmark in `bench/<component>/`.
- When proposing a design change, state the expected effect on latency or memory and how to measure it.
- Record significant design decisions in `docs/decisions.md` (date, decision, reasoning, alternatives considered).
- With the exception of editing .cpp/.hpp files or CMakeLists.txt, do not run commands that change the state of the repo, such as: building the project yourself, running tests yourself, git adding/commiting/pushing yourself, installing packages yourself, etc. If you're not sure, suggest the command for me to run instead.
- Do not go beyond the scope of a question. This means things like pointing out bugs after you've answered a question unrealted to bugs.
- Don't hint at fixes for bugs in my code unless I ask for one. That includes indirect hints like "once X does Y, this will pass" or pointing at the line to look at. If I ask why something fails, explain what's going wrong without saying how to fix it.
