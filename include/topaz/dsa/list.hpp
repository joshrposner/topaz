#pragma once

#include <cstddef>
#include <iterator>
#include <memory>
#include <utility>

#include "../defs.hpp"

namespace tpz::dsa {

/// @brief Doubly linked, non-intrusive list.
///
/// Each element lives in its own heap-allocated node, and the list owns a copy
/// of every element. List can be moved but not copied. A moved-from list is
/// valid, empty, and can be used again. Destroying the list destroys every
/// element exactly once. Destruction is iterative, so a very long list does not
/// overflow the stack.
///
/// @tparam T Element type. Must be copy-constructible. Does not need a default
/// constructor.
template <class T>
class List {
 private:
  /// @brief Internal node of List. Owns the next node; points back to the
  /// previous one.
  /// @tparam T Element type.
  template <class T_>
  struct Node {
   public:
    T_ val_;
    Node<T_>* prev_;
    std::unique_ptr<Node<T_>> next_;

    /// @brief Creates a node holding @p val, linked between @p prev and @p
    /// next.
    /// @param val Value to store.
    /// @param prev Previous node (not owned), or nullptr if this is the first
    /// node.
    /// @param next Next node, whose ownership passes to this node; nullptr if
    /// this is the last node.
    Node(T_ val, Node<T_>* prev = nullptr,
         std::unique_ptr<Node<T_>> next = nullptr)
        : val_(std::move(val)), prev_(prev), next_(std::move(next)) {}

    /// @brief Not copyable: a node uniquely owns the rest of the chain.
    Node(const Node& other) = delete;

    /// @brief Takes over @p other's links and copies its value.
    /// @note @p other keeps its @c prev_ pointer, but its @c next_ becomes
    /// null.
    Node(Node&& other) noexcept
        : val_(std::move(other.val_)),
          prev_(other.prev_),
          next_(std::move(other.next_)) {}

    /// @brief Not copyable: a node uniquely owns the rest of the chain.
    Node& operator=(const Node& other) = delete;

    /// @brief Takes over @p other's links and copies its value, freeing this
    /// node's old successor.
    /// @note @p other keeps its @c prev_ pointer, but its @c next_ becomes
    /// null.
    Node& operator=(Node&& other) noexcept {
      val_ = other.val_;
      prev_ = other.prev_;
      next_ = std::move(other.next_);
      return *this;
    }
  };

  std::unique_ptr<Node<T>> head_;
  Node<T>* tail_;
  std::size_t size_;

 public:
  /// @brief Creates an empty list. Does not allocate.
  List() noexcept : head_(nullptr), tail_(nullptr), size_(0) {}

  /// @brief Not copyable.
  List(const List& other) = delete;

  /// @brief Takes over @p other's elements. @p other is left empty.
  /// @note O(1). Does not allocate, copy, or destroy any elements. Iterators
  /// into @p other stay
  ///       valid and now refer to elements of this list.
  List(List&& other) noexcept
      : head_(std::move(other.head_)),
        tail_(std::exchange(other.tail_, nullptr)),
        size_(std::exchange(other.size_, 0)) {}

  /// @brief Not copyable.
  List& operator=(const List& other) = delete;

  /// @brief Destroys this list's elements, then takes over @p other's. @p other
  /// is left empty.
  /// @return `*this`.
  /// @note O(n) in this list's old size, O(1) in @p other's size. Invalidates
  /// all iterators into
  ///       this list. Iterators into @p other stay valid and now refer to
  ///       elements of this list.
  List& operator=(List&& other) noexcept {
    clear();
    head_ = std::move(other.head_);
    tail_ = std::exchange(other.tail_, nullptr);
    size_ = std::exchange(other.size_, 0);
    return *this;
  }

  /// @brief Bidirectional iterator over the list's elements.
  ///
  /// Dereferencing yields `T&`, so the list works with range-for and
  /// `<algorithm>`. An iterator stays valid until the element it refers to is
  /// removed. Adding or removing other elements does not invalidate it.
  struct Iterator {
    // mandatory C++20/23 public type aliases
    using iterator_category = std::bidirectional_iterator_tag;
    using iterator_concept = std::bidirectional_iterator_tag;
    using difference_type = std::ptrdiff_t;
    using value_type = T;
    using pointer = T*;
    using reference = T&;

    Node<T>* node_;
    pointer ptr_;

    /// @brief Returns the element this iterator refers to.
    /// @pre The iterator is not end().
    reference operator*() const { return *ptr_; }

    /// @brief Accesses a member of the element this iterator refers to.
    /// @pre The iterator is not end().
    pointer operator->() const { return ptr_; }

    /// @brief Advances to the next element (`++it`).
    /// @return `*this`, now referring to the next element, or end() if this was
    /// the last.
    /// @pre The iterator is not end().
    Iterator& operator++() {
      TPZ_ASSERT(node_ != nullptr);

      node_ = node_->next_.get();
      ptr_ = !node_ ? nullptr : &node_->val_;
      return *this;
    }

    /// @brief Advances to the next element (`it++`).
    /// @return A copy of the iterator from before it advanced.
    /// @pre The iterator is not end().
    Iterator operator++(int) {
      Iterator tmp = *this;
      ++(*this);
      return tmp;
    }

    /// @brief Moves back to the previous element (`--it`).
    /// @return `*this`, now referring to the previous element.
    /// @pre The iterator is not end() and not begin().
    /// @note Unlike std::list, decrementing end() is not supported, because
    /// end() does not know
    ///       which list it belongs to.
    Iterator& operator--() {
      TPZ_ASSERT(node_ != nullptr);

      node_ = node_->prev_;
      ptr_ = !node_ ? nullptr : &node_->val_;
      return *this;
    }

    /// @brief Moves back to the previous element (`it--`).
    /// @return A copy of the iterator from before it moved.
    /// @pre The iterator is not end() and not begin().
    Iterator operator--(int) {
      Iterator tmp = *this;
      --(*this);
      return tmp;
    }

    /// @brief Returns whether both iterators refer to the same element, or are
    /// both end().
    friend bool operator==(const Iterator& lhs, const Iterator& rhs) {
      return lhs.ptr_ == rhs.ptr_ && lhs.node_ == rhs.node_;
    }

    /// @brief Returns whether the iterators refer to different elements.
    friend bool operator!=(const Iterator& lhs, const Iterator& rhs) {
      return lhs.ptr_ != rhs.ptr_ || lhs.node_ != rhs.node_;
    }
  };

  /// @brief Returns an iterator to the first element.
  /// @return Iterator to the first element, or end() if the list is empty.
  Iterator begin() {
    return Iterator{head_.get(), !head_ ? nullptr : &head_->val_};
  }

  /// @brief Returns the past-the-end iterator.
  /// @return Iterator one past the last element. It must not be dereferenced.
  Iterator end() { return Iterator{nullptr, nullptr}; }

  /// @brief Inserts a copy of @p val at the front of the list.
  /// @param val Value to copy into the new first element.
  /// @note O(1). Allocates one node. Does not invalidate any iterators.
  void push_front(const T& val) {
    if (size_ == 0) {
      head_ = std::make_unique<Node<T>>(val);
      tail_ = head_.get();
    } else {
      Node<T>* old_head = head_.get();
      auto new_head = std::make_unique<Node<T>>(val, nullptr, std::move(head_));
      old_head->prev_ = new_head.get();
      head_ = std::move(new_head);
    }

    size_++;
  }

  /// @brief Inserts a copy of @p val at the back of the list.
  /// @param val Value to copy into the new last element.
  /// @note O(1). Allocates one node. Does not invalidate any iterators.
  void push_back(const T& val) {
    if (size_ == 0) {
      head_ = std::make_unique<Node<T>>(val);
      tail_ = head_.get();
    } else {
      Node<T>* new_tail = new Node<T>(val, tail_, nullptr);
      tail_->next_.reset(new_tail);
      tail_ = new_tail;
    }

    size_++;
  }

  /// @brief Removes and destroys the first element.
  /// @pre `!empty()`
  /// @note O(1). Invalidates only iterators to the removed element.
  void pop_front() {
    TPZ_ASSERT(head_ != nullptr);

    if (size_ == 1) {
      head_.reset(nullptr);
      tail_ = nullptr;
    } else {
      head_ = std::move(head_->next_);
      head_->prev_ = nullptr;
    }

    size_--;
  }

  /// @brief Removes and destroys the last element.
  /// @pre `!empty()`
  /// @note O(1). Invalidates only iterators to the removed element.
  void pop_back() {
    TPZ_ASSERT(head_ != nullptr);

    if (size_ == 1) {
      head_.reset(nullptr);
      tail_ = nullptr;
    } else {
      tail_ = tail_->prev_;
      tail_->next_.reset(nullptr);
    }

    size_--;
  }

  /// @brief Removes and destroys the element at @p it.
  /// @param it Iterator to the element to remove.
  /// @return Iterator to the element after the removed one, or end() if the
  /// removed element was
  ///         last.
  /// @pre @p it refers to an element of this list (and is not end()).
  /// @note O(1). Invalidates only @p it and copies of it.
  Iterator erase(Iterator it) {
    TPZ_ASSERT(head_ != nullptr);
    TPZ_ASSERT(it.node_ != nullptr && it.ptr_ != nullptr);

    Iterator next_it;

    if (size_ == 1 || it.node_ == head_.get()) {
      pop_front();  // this executes size_--
      next_it = begin();
    } else if (it.node_ == tail_) {
      pop_back();  // this executes size_--
      next_it = end();
    } else {
      next_it = Iterator{it.node_->next_.get(), &it.node_->next_->val_};
      it.node_->next_->prev_ = it.node_->prev_;
      it.node_->prev_->next_ = std::move(it.node_->next_);
      size_--;
    }

    return next_it;
  }

  /// @brief Returns the first element, read-only.
  /// @return Const reference to the first element.
  /// @pre `!empty()`
  const T& front() const {
    TPZ_ASSERT(size_ != 0);
    return head_->val_;
  }

  /// @brief Returns the first element.
  /// @return Mutable reference to the first element, valid until that element
  /// is removed.
  /// @pre `!empty()`
  T& front() {
    TPZ_ASSERT(size_ != 0);
    return head_->val_;
  }

  /// @brief Returns the last element, read-only.
  /// @return Const reference to the last element.
  /// @pre `!empty()`
  const T& back() const {
    TPZ_ASSERT(size_ != 0);
    return tail_->val_;
  }

  /// @brief Returns the last element.
  /// @return Mutable reference to the last element, valid until that element is
  /// removed.
  /// @pre `!empty()`
  T& back() {
    TPZ_ASSERT(size_ != 0);
    return tail_->val_;
  }

  /// @brief Removes and destroys every element. The list is empty afterwards
  /// and can be reused.
  /// @note O(n). Iterative, so a very long list does not overflow the stack.
  /// Invalidates all
  ///       iterators except end().
  void clear() {
    if (size_ == 0) return;

    Node<T>* p = tail_->prev_;
    while (p) {
      p->next_ = nullptr;
      p = p->prev_;
    }
    head_ = nullptr;
    tail_ = nullptr;
    size_ = 0;
  }

  /// @brief Returns the number of elements. O(1).
  std::size_t size() const noexcept { return size_; }

  /// @brief Returns whether the list has no elements. O(1).
  bool empty() const noexcept { return size_ == 0; }

  /// @brief Destroys every element exactly once. Iterative; see clear().
  ~List() { clear(); }
};

}  // namespace tpz::dsa
