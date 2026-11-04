#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <topaz/dsa/list.hpp>
#include <type_traits>
#include <utility>
#include <vector>

// Contract these tests assume:
//   - *it returns T& (not the node), so range-for and <algorithm> work on
//   values
//   - front() / back() return a reference to the value; calling them on an
//   empty list is a
//     precondition violation (not tested)
//   - erase(it) returns an iterator to the element after the erased one (end()
//   if it was last)
//   - List is move-only: moves leave the source valid and empty
//   - T does not need a default constructor

using tpz::dsa::List;

// Compiles every member function with T = int, so errors in code no test calls
// yet still show up.
template class tpz::dsa::List<int>;

namespace {

// Checks size(), empty(), iteration order, front() and back() against
// `expected`. Iteration stops one step past the expected length, so a list that
// never reaches end() fails instead of hanging.
testing::AssertionResult has_contents(List<int>& list,
                                      const std::vector<int>& expected) {
  if (list.size() != expected.size()) {
    return testing::AssertionFailure()
           << "size() is " << list.size() << ", expected " << expected.size();
  }
  if (list.empty() != expected.empty()) {
    return testing::AssertionFailure() << "empty() is " << list.empty();
  }

  std::vector<int> actual;
  for (auto it = list.begin();
       it != list.end() && actual.size() <= expected.size(); ++it) {
    actual.push_back(*it);
  }
  if (actual != expected) {
    return testing::AssertionFailure()
           << "iteration gives " << testing::PrintToString(actual)
           << ", expected " << testing::PrintToString(expected);
  }

  if (!expected.empty()) {
    if (list.front() != expected.front()) {
      return testing::AssertionFailure() << "front() is " << list.front()
                                         << ", expected " << expected.front();
    }
    if (list.back() != expected.back()) {
      return testing::AssertionFailure()
             << "back() is " << list.back() << ", expected " << expected.back();
    }
  }
  return testing::AssertionSuccess();
}

List<int> make_list(const std::vector<int>& values) {
  List<int> list;
  for (int v : values) list.push_back(v);
  return list;
}

// Counts live instances, so tests can prove every element is destroyed exactly
// once. A double destroy drives LIVE negative. Also has no default constructor.
struct Tracked {
  static inline int LIVE = 0;
  int value;

  explicit Tracked(int v) : value(v) { ++LIVE; }
  Tracked(const Tracked& other) : value(other.value) { ++LIVE; }
  Tracked(Tracked&& other) noexcept : value(other.value) { ++LIVE; }
  Tracked& operator=(const Tracked&) = default;
  Tracked& operator=(Tracked&&) = default;
  ~Tracked() { --LIVE; }
};

class ListLifetimeTest : public testing::Test {
 protected:
  void SetUp() override { Tracked::LIVE = 0; }
  void TearDown() override {
    EXPECT_EQ(Tracked::LIVE, 0) << "elements leaked or destroyed twice";
  }
};

}  // namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

TEST(ListTest, NewListIsEmpty) {
  List<int> list;
  EXPECT_TRUE(has_contents(list, {}));
  EXPECT_TRUE(list.begin() == list.end());
}

// ---------------------------------------------------------------------------
// push_front / push_back
// ---------------------------------------------------------------------------

TEST(ListTest, PushBackSingle) {
  List<int> list;
  list.push_back(7);
  EXPECT_TRUE(has_contents(list, {7}));
}

TEST(ListTest, PushFrontSingle) {
  List<int> list;
  list.push_front(7);
  EXPECT_TRUE(has_contents(list, {7}));
}

TEST(ListTest, PushBackPreservesOrder) {
  List<int> list = make_list({1, 2, 3, 4, 5});
  EXPECT_TRUE(has_contents(list, {1, 2, 3, 4, 5}));
}

TEST(ListTest, PushFrontReversesOrder) {
  List<int> list;
  for (int v : {1, 2, 3, 4, 5}) list.push_front(v);
  EXPECT_TRUE(has_contents(list, {5, 4, 3, 2, 1}));
}

TEST(ListTest, MixedPushes) {
  List<int> list;
  list.push_back(3);
  list.push_front(2);
  list.push_back(4);
  list.push_front(1);
  EXPECT_TRUE(has_contents(list, {1, 2, 3, 4}));
}

// ---------------------------------------------------------------------------
// pop_front / pop_back
// ---------------------------------------------------------------------------

TEST(ListTest, PopFrontRemovesHead) {
  List<int> list = make_list({1, 2, 3});
  list.pop_front();
  EXPECT_TRUE(has_contents(list, {2, 3}));
}

TEST(ListTest, PopBackRemovesTail) {
  List<int> list = make_list({1, 2, 3});
  list.pop_back();
  EXPECT_TRUE(has_contents(list, {1, 2}));
}

TEST(ListTest, PopFrontToEmpty) {
  List<int> list = make_list({1, 2, 3});
  list.pop_front();
  list.pop_front();
  list.pop_front();
  EXPECT_TRUE(has_contents(list, {}));
  EXPECT_TRUE(list.begin() == list.end());
}

TEST(ListTest, PopBackToEmpty) {
  List<int> list = make_list({1, 2, 3});
  list.pop_back();
  list.pop_back();
  list.pop_back();
  EXPECT_TRUE(has_contents(list, {}));
  EXPECT_TRUE(list.begin() == list.end());
}

TEST(ListTest, PopAlternatingEnds) {
  List<int> list = make_list({1, 2, 3, 4, 5});
  list.pop_front();
  EXPECT_TRUE(has_contents(list, {2, 3, 4, 5}));
  list.pop_back();
  EXPECT_TRUE(has_contents(list, {2, 3, 4}));
  list.pop_front();
  EXPECT_TRUE(has_contents(list, {3, 4}));
  list.pop_back();
  EXPECT_TRUE(has_contents(list, {3}));
  list.pop_front();
  EXPECT_TRUE(has_contents(list, {}));
}

TEST(ListTest, PushBackThenPopFront) {
  List<int> list;
  list.push_back(5);
  list.pop_front();
  EXPECT_TRUE(has_contents(list, {}));
}

TEST(ListTest, PushFrontThenPopBack) {
  List<int> list;
  list.push_front(5);
  list.pop_back();
  EXPECT_TRUE(has_contents(list, {}));
}

// After emptying, head and tail must both be reset, not left pointing at freed
// nodes.
TEST(ListTest, ReusableAfterPopFrontToEmpty) {
  List<int> list = make_list({1, 2});
  list.pop_front();
  list.pop_front();
  list.push_back(9);
  EXPECT_TRUE(has_contents(list, {9}));
  list.push_front(8);
  EXPECT_TRUE(has_contents(list, {8, 9}));
}

TEST(ListTest, ReusableAfterPopBackToEmpty) {
  List<int> list = make_list({1, 2});
  list.pop_back();
  list.pop_back();
  list.push_front(9);
  EXPECT_TRUE(has_contents(list, {9}));
  list.push_back(10);
  EXPECT_TRUE(has_contents(list, {9, 10}));
}

// ---------------------------------------------------------------------------
// front / back
// ---------------------------------------------------------------------------

TEST(ListTest, FrontAndBackAreAssignable) {
  List<int> list = make_list({1, 2, 3});
  list.front() = 10;
  list.back() = 30;
  EXPECT_TRUE(has_contents(list, {10, 2, 30}));
}

// With one element, front() and back() must refer to the same value, not two
// copies.
TEST(ListTest, FrontAndBackAliasSingleElement) {
  List<int> list = make_list({1});
  list.front() = 5;
  EXPECT_EQ(list.back(), 5);
  EXPECT_EQ(&list.front(), &list.back());
}

TEST(ListTest, ConstFrontAndBackAreReadOnly) {
  List<int> list = make_list({1, 2, 3});
  const List<int>& view = list;
  static_assert(std::is_same_v<decltype(view.front()), const int&>);
  static_assert(std::is_same_v<decltype(view.back()), const int&>);
  EXPECT_EQ(view.front(), 1);
  EXPECT_EQ(view.back(), 3);
}

// ---------------------------------------------------------------------------
// Iteration
// ---------------------------------------------------------------------------

TEST(ListTest, RangeForVisitsEveryElement) {
  List<int> list = make_list({1, 2, 3, 4, 5});
  int sum = 0;
  int count = 0;
  for (int v : list) {
    sum += v;
    ++count;
  }
  EXPECT_EQ(sum, 15);
  EXPECT_EQ(count, 5);
}

TEST(ListTest, IteratorDereferenceIsAssignable) {
  List<int> list = make_list({1, 2, 3});
  for (int& v : list) v *= 10;
  EXPECT_TRUE(has_contents(list, {10, 20, 30}));
}

TEST(ListTest, PostIncrementReturnsOldPosition) {
  List<int> list = make_list({1, 2});
  auto it = list.begin();
  auto old = it++;
  EXPECT_EQ(*old, 1);
  EXPECT_EQ(*it, 2);
}

TEST(ListTest, IteratorModelsForwardIterator) {
  EXPECT_TRUE(std::forward_iterator<List<int>::Iterator>);
  // If you make the iterator bidirectional (so --end() works), also check:
  // EXPECT_TRUE(std::bidirectional_iterator<List<int>::Iterator>);
}

TEST(ListTest, WorksWithStdFind) {
  List<int> list = make_list({1, 2, 3, 4, 5});
  auto found = std::find(list.begin(), list.end(), 3);
  ASSERT_TRUE(found != list.end());
  EXPECT_EQ(*found, 3);
  EXPECT_TRUE(std::find(list.begin(), list.end(), 42) == list.end());
}

TEST(ListTest, WorksWithStdDistance) {
  List<int> list = make_list({1, 2, 3, 4, 5});
  EXPECT_EQ(std::distance(list.begin(), list.end()), 5);
}

// ---------------------------------------------------------------------------
// erase
// ---------------------------------------------------------------------------

TEST(ListTest, EraseOnlyElement) {
  List<int> list = make_list({1});
  auto next = list.erase(list.begin());
  EXPECT_TRUE(next == list.end());
  EXPECT_TRUE(has_contents(list, {}));
}

TEST(ListTest, EraseHead) {
  List<int> list = make_list({1, 2, 3});
  auto next = list.erase(list.begin());
  ASSERT_TRUE(next != list.end());
  EXPECT_EQ(*next, 2);
  EXPECT_TRUE(has_contents(list, {2, 3}));
}

TEST(ListTest, EraseTail) {
  List<int> list = make_list({1, 2, 3});
  auto next = list.erase(std::find(list.begin(), list.end(), 3));
  EXPECT_TRUE(next == list.end());
  EXPECT_TRUE(has_contents(list, {1, 2}));
}

TEST(ListTest, EraseMiddle) {
  List<int> list = make_list({1, 2, 3});
  auto next = list.erase(std::find(list.begin(), list.end(), 2));
  ASSERT_TRUE(next != list.end());
  EXPECT_EQ(*next, 3);
  EXPECT_TRUE(has_contents(list, {1, 3}));
}

// Erasing the tail must move tail back, so the next push_back links to the
// right node.
TEST(ListTest, PushBackAfterEraseTail) {
  List<int> list = make_list({1, 2, 3});
  list.erase(std::find(list.begin(), list.end(), 3));
  list.push_back(9);
  EXPECT_TRUE(has_contents(list, {1, 2, 9}));
}

TEST(ListTest, PushFrontAfterEraseHead) {
  List<int> list = make_list({1, 2, 3});
  list.erase(list.begin());
  list.push_front(9);
  EXPECT_TRUE(has_contents(list, {9, 2, 3}));
}

// Erasing the middle must fix both neighbours' links, in both directions.
TEST(ListTest, PopBothEndsAfterEraseMiddle) {
  List<int> list = make_list({1, 2, 3});
  list.erase(std::find(list.begin(), list.end(), 2));
  list.pop_back();
  EXPECT_TRUE(has_contents(list, {1}));
  list.pop_front();
  EXPECT_TRUE(has_contents(list, {}));
}

TEST(ListTest, EraseWhileIterating) {
  List<int> list = make_list({1, 2, 3, 4, 5, 6});
  for (auto it = list.begin(); it != list.end();) {
    if (*it % 2 == 0) {
      it = list.erase(it);
    } else {
      ++it;
    }
  }
  EXPECT_TRUE(has_contents(list, {1, 3, 5}));
}

TEST(ListTest, EraseEverythingThenReuse) {
  List<int> list = make_list({1, 2, 3});
  for (auto it = list.begin(); it != list.end();) it = list.erase(it);
  EXPECT_TRUE(has_contents(list, {}));
  list.push_back(4);
  list.push_front(3);
  EXPECT_TRUE(has_contents(list, {3, 4}));
}

// ---------------------------------------------------------------------------
// Move semantics
// ---------------------------------------------------------------------------

TEST(ListTest, IsMoveOnly) {
  EXPECT_TRUE(std::is_move_constructible_v<List<int>>);
  EXPECT_TRUE(std::is_move_assignable_v<List<int>>);
  // Change these if you decide to implement copying.
  EXPECT_FALSE(std::is_copy_constructible_v<List<int>>);
  EXPECT_FALSE(std::is_copy_assignable_v<List<int>>);
}

TEST(ListTest, MoveConstructLeavesSourceEmpty) {
  List<int> src = make_list({1, 2, 3});
  List<int> dst(std::move(src));
  EXPECT_TRUE(has_contents(dst, {1, 2, 3}));
  EXPECT_TRUE(has_contents(
      src, {}));  // NOLINT(bugprone-use-after-move): testing moved-from state
}

TEST(ListTest, MovedFromListIsReusable) {
  List<int> src = make_list({1, 2, 3});
  List<int> dst(std::move(src));
  src.push_back(9);  // NOLINT(bugprone-use-after-move)
  src.push_front(8);
  EXPECT_TRUE(has_contents(src, {8, 9}));
  EXPECT_TRUE(has_contents(dst, {1, 2, 3}));
}

TEST(ListTest, MoveAssignReplacesContents) {
  List<int> src = make_list({1, 2, 3});
  List<int> dst = make_list({7, 8});
  dst = std::move(src);
  EXPECT_TRUE(has_contents(dst, {1, 2, 3}));
  EXPECT_TRUE(has_contents(src, {}));  // NOLINT(bugprone-use-after-move)
}

TEST(ListTest, PushBackAfterMoveAssign) {
  List<int> src = make_list({1, 2});
  List<int> dst;
  dst = std::move(src);
  dst.push_back(3);
  EXPECT_TRUE(has_contents(dst, {1, 2, 3}));
}

// ---------------------------------------------------------------------------
// Element lifetime (Tracked counts constructions and destructions)
// ---------------------------------------------------------------------------

TEST_F(ListLifetimeTest, DestructorDestroysEveryElement) {
  {
    List<Tracked> list;
    for (int i = 0; i < 5; ++i) list.push_back(Tracked(i));
    EXPECT_EQ(Tracked::LIVE, 5);
  }
  // TearDown checks LIVE == 0
}

TEST_F(ListLifetimeTest, PopDestroysElement) {
  List<Tracked> list;
  list.push_back(Tracked(1));
  list.push_back(Tracked(2));
  list.push_back(Tracked(3));
  list.pop_front();
  EXPECT_EQ(Tracked::LIVE, 2);
  list.pop_back();
  EXPECT_EQ(Tracked::LIVE, 1);
  list.pop_back();
  EXPECT_EQ(Tracked::LIVE, 0);
}

TEST_F(ListLifetimeTest, EraseDestroysElement) {
  List<Tracked> list;
  list.push_back(Tracked(1));
  list.push_back(Tracked(2));
  list.push_back(Tracked(3));
  auto it = list.begin();
  ++it;
  list.erase(it);
  EXPECT_EQ(Tracked::LIVE, 2);
}

TEST_F(ListLifetimeTest, PushStoresACopy) {
  List<Tracked> list;
  Tracked original(1);
  list.push_back(original);
  original.value = 99;
  EXPECT_EQ(list.front().value, 1);
}

TEST_F(ListLifetimeTest, MoveConstructDoesNotCopyOrDestroyElements) {
  List<Tracked> src;
  src.push_back(Tracked(1));
  src.push_back(Tracked(2));
  List<Tracked> dst(std::move(src));
  EXPECT_EQ(Tracked::LIVE, 2);
}

TEST_F(ListLifetimeTest, MoveAssignDestroysOldContents) {
  List<Tracked> src;
  src.push_back(Tracked(1));
  src.push_back(Tracked(2));
  src.push_back(Tracked(3));
  List<Tracked> dst;
  dst.push_back(Tracked(7));
  dst.push_back(Tracked(8));
  EXPECT_EQ(Tracked::LIVE, 5);
  dst = std::move(src);
  EXPECT_EQ(Tracked::LIVE, 3);
}

// ---------------------------------------------------------------------------
// Scale
// ---------------------------------------------------------------------------

// A price level can hold a lot of orders. Destroying the list must not use one
// stack frame per node, or this overflows the stack.
TEST(ListTest, LongListDestroysWithoutStackOverflow) {
  constexpr int COUNT = 1'000'000;
  List<int> list;
  for (int i = 0; i < COUNT; ++i) list.push_back(i);
  EXPECT_EQ(list.size(), static_cast<std::size_t>(COUNT));
}

TEST(ListTest, LongListMoveAssignWithoutStackOverflow) {
  constexpr int COUNT = 1'000'000;
  List<int> dst;
  for (int i = 0; i < COUNT; ++i) dst.push_back(i);
  dst = make_list({1});
  EXPECT_TRUE(has_contents(dst, {1}));
}
