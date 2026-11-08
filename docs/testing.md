# Testing

Unit tests use [GoogleTest](https://github.com/google/googletest), fetched via `FetchContent` in [`tests/CMakeLists.txt`](../tests/CMakeLists.txt). Everything builds into one executable, `tpz_tests`.

## Running

**Rebuild before running.** CTest only knows about tests compiled into the current `tpz_tests` binary. A new test won't show up until you build.

```sh
cmake --build build --config Release -j
ctest --test-dir build -C Release --output-on-failure
```

`-C Release` / `--config Release` are required with the Visual Studio generator (multi-config). They're harmless elsewhere.

Useful variants:

```sh
ctest --test-dir build -C Release -N              # list tests without running
ctest --test-dir build -C Release -R Smoke        # run tests matching a regex

# Run the binary directly for GoogleTest's own output and flags
./build/tests/tpz_tests --gtest_filter=Smoke.*    # wildcards; prefix patterns with - to exclude
```

The binary may be under `build/tests/Release/` with the Visual Studio generator.

## How it fits together

- `TEST(Suite, Name)` defines a function and registers it in a global list when the program starts. You never call it yourself.
- `GTest::gtest_main` supplies `main()`, which runs every registered test. Don't write your own `main`.
- `gtest_discover_tests(tpz_tests)` runs the built binary with `--gtest_list_tests` after each build and makes each test its own CTest entry.

## Adding a test

1. Create `tests/<component>/<name>_test.cpp`, mirroring `include/topaz/`. For example, `include/topaz/dsa/list.hpp` is tested in `tests/dsa/list_test.cpp`, and core engine headers directly in `include/topaz/` are tested directly in `tests/` (`include/topaz/order_book.hpp` → `tests/order_book_test.cpp`).
2. **Add the file to `add_executable(tpz_tests ...)`** in `tests/CMakeLists.txt`. Sources are listed explicitly with no globbing, so an unlisted file is silently skipped.
3. Rebuild and run.

## Assertions

- `EXPECT_*` records a failure and keeps going.
- `ASSERT_*` records a failure and returns from the test immediately. Use it when continuing would be meaningless or crash, e.g. `ASSERT_TRUE(result.has_value());` before dereferencing.
- Common ones: `EXPECT_EQ/NE/LT/LE/GT/GE`, `EXPECT_TRUE/FALSE`, `EXPECT_THROW`.
- Stream context into a failure message:

  ```cpp
  EXPECT_EQ(book.best_bid(), 100) << "after cancelling order " << id;
  ```

## Fixtures

Use a fixture when several tests share setup:

```cpp
class OrderBookTest : public ::testing::Test {
protected:
    void SetUp() override { /* runs before each test */ }
    tpz::OrderBook book_;
};

TEST_F(OrderBookTest, EmptyBookHasNoBestBid) {
    EXPECT_FALSE(book_.best_bid().has_value());
}
```

Each `TEST_F` gets a freshly constructed fixture, so tests never share state and don't depend on run order.

For tables of cases (e.g. "this order sequence produces these fills"), use parameterized tests: `TEST_P` with `INSTANTIATE_TEST_SUITE_P`.

## Guidelines

- Name tests after the behavior they check, e.g. `Matching.IncomingBuyFillsBestAskFirst`. A failure report should say what broke without opening the file.
- Keep one behavior per test. Several assertions are fine if they check the same rule.
- Every matching rule gets a test before it gets optimized.
- Put shared helpers and fixtures in `tests/common/`.
- Include what you use (`<cstdlib>` for `std::abs`, etc.). Indirect includes differ between GCC, Clang, and MSVC.
- Periodically run the tests under the sanitizer build:

  ```sh
  cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug -DTPZ_ENABLE_SANITIZERS=ON
  cmake --build build-debug --config Debug -j
  ctest --test-dir build-debug -C Debug --output-on-failure
  ```
