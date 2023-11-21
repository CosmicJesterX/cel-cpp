// Copyright 2023 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef THIRD_PARTY_CEL_CPP_COMMON_SIZED_INPUT_VIEW_H_
#define THIRD_PARTY_CEL_CPP_COMMON_SIZED_INPUT_VIEW_H_

#include <cstddef>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <new>
#include <type_traits>

#include "absl/base/attributes.h"
#include "absl/base/optimization.h"
#include "absl/log/absl_check.h"

namespace cel {

// `SizedInputView` is a type-erased, read-only view for forward sized iterable
// ranges. This should be useful for handling different container types when
// the alternatives are cumbersome or impossible.
template <typename T>
class SizedInputView;

namespace sized_input_view_internal {

template <typename C>
using ConstIterator =
    std::decay_t<decltype(std::begin(std::declval<const C&>()))>;

template <typename C>
using SizeType = std::decay_t<decltype(std::size(std::declval<const C&>()))>;

template <typename T, typename Iter>
constexpr bool CanIterateAsType() {
  return std::is_convertible_v<
             std::add_pointer_t<decltype(*std::declval<Iter>())>, const T*> ||
         std::is_convertible_v<decltype(*std::declval<Iter>()), T>;
}

inline constexpr size_t kSmallSize = sizeof(void*) * 2;

union Storage {
  char small[kSmallSize];
  void* large;
};

template <typename T>
constexpr bool IsStoredInline() {
  return alignof(T) <= alignof(Storage) && sizeof(T) <= kSmallSize;
}

template <typename T>
T* StorageCast(Storage& storage) {
  if constexpr (IsStoredInline<T>()) {
    return std::launder(reinterpret_cast<T*>(&storage.small[0]));
  } else {
    return static_cast<T*>(storage.large);
  }
}

template <typename T>
const T* StorageCast(const Storage& storage) {
  if constexpr (IsStoredInline<T>()) {
    return std::launder(reinterpret_cast<const T*>(&storage.small[0]));
  } else {
    return static_cast<const T*>(storage.large);
  }
}

template <typename T, typename Iter>
constexpr bool IsValueStashRequired() {
  return !std::is_convertible_v<
      std::add_pointer_t<decltype(*std::declval<Iter>())>, const T*>;
}

template <typename Iter>
struct LargeIteratorStorage {
  alignas(Iter) char begin[sizeof(Iter)];
  alignas(Iter) char end[sizeof(Iter)];
};

template <typename T>
struct LargeValueStorage {
  alignas(T) char value[sizeof(T)];
};

template <typename T, typename Iter>
struct LargeStorage : LargeIteratorStorage<Iter>, LargeValueStorage<T> {};

struct RangeStorage {
  Storage begin;
  Storage end;
  Storage value_stash;
};

template <typename T>
char* Allocate() {
  return static_cast<char*>(
      ::operator new(sizeof(T), static_cast<std::align_val_t>(alignof(T))));
}

template <typename T>
void Deallocate(void* address) {
#if defined(__cpp_sized_deallocation) && __cpp_sized_deallocation >= 201309L
  ::operator delete(address, sizeof(T),
                    static_cast<std::align_val_t>(alignof(T)));
#else
  ::operator delete(address, static_cast<std::align_val_t>(alignof(T)));
#endif
}

template <typename T, typename Iter>
void CreateRangeStorage(RangeStorage* range) {
  if constexpr ((!IsValueStashRequired<T, Iter>() || IsStoredInline<T>()) &&
                IsStoredInline<Iter>()) {
    // Nothing.
  } else if constexpr (!IsValueStashRequired<T, Iter>() ||
                       IsStoredInline<T>()) {
    using StorageType = LargeIteratorStorage<Iter>;
    auto* storage = Allocate<StorageType>();
    range->begin.large = storage + offsetof(StorageType, begin);
    range->end.large = storage + offsetof(StorageType, end);
  } else if constexpr (IsStoredInline<Iter>()) {
    using StorageType = LargeValueStorage<T>;
    auto* storage = Allocate<StorageType>();
    range->value_stash.large = storage + offsetof(StorageType, value);
  } else {
    using StorageType = LargeStorage<T, Iter>;
    auto* storage = Allocate<StorageType>();
    range->begin.large = storage + offsetof(StorageType, begin);
    range->end.large = storage + offsetof(StorageType, end);
    range->value_stash.large = storage + offsetof(StorageType, value);
  }
}

template <typename T, typename Iter>
void DestroyRangeStorage(RangeStorage* range) {
  if constexpr ((!IsValueStashRequired<T, Iter>() || IsStoredInline<T>()) &&
                IsStoredInline<Iter>()) {
    // Nothing.
  } else if constexpr (!IsValueStashRequired<T, Iter>() ||
                       IsStoredInline<T>()) {
    using StorageType = LargeIteratorStorage<Iter>;
    auto* storage =
        static_cast<char*>(range->begin.large) - offsetof(StorageType, begin);
    Deallocate<StorageType>(storage);
  } else if constexpr (IsStoredInline<Iter>()) {
    using StorageType = LargeValueStorage<T>;
    auto* storage = static_cast<char*>(range->value_stash.large) -
                    offsetof(StorageType, value);
    Deallocate<StorageType>(storage);
  } else {
    using StorageType = LargeStorage<T, Iter>;
    auto* storage =
        static_cast<char*>(range->begin.large) - offsetof(StorageType, begin);
    Deallocate<StorageType>(storage);
  }
}

enum class Operation {
  kCreate,
  kAdvanceOne,
  kCopy,
  kMove,
  kDestroy,
};

union OperationInput {
  struct {
    RangeStorage* storage;
    void* begin;
    void* end;
  } create;
  RangeStorage* advance_one;
  struct {
    const RangeStorage* src;
    RangeStorage* dest;
  } copy;
  struct {
    RangeStorage* src;
    RangeStorage* dest;
  } move;
  RangeStorage* destroy;
};

union OperationOutput {
  const void* value;
};

using RangeManagerFn = OperationOutput (*)(Operation, OperationInput);

template <typename T, typename Iter>
void RangeManagerDestroy(RangeStorage* range) {
  if constexpr (IsValueStashRequired<T, Iter>()) {
    StorageCast<T>(range->value_stash)->~T();
  }
  StorageCast<Iter>(range->end)->~Iter();
  StorageCast<Iter>(range->begin)->~Iter();
  DestroyRangeStorage<T, Iter>(range);
}

template <typename T, typename Iter>
const void* RangeManagerAdvanceOne(RangeStorage* range) {
  auto* begin = StorageCast<Iter>(range->begin);
  auto* end = StorageCast<Iter>(range->end);
  if (++(*begin) == *end) {
    RangeManagerDestroy<T, Iter>(range);
    return nullptr;
  } else {
    if constexpr (IsValueStashRequired<T, Iter>()) {
      auto* value_stash = StorageCast<T>(range->value_stash);
      value_stash->~T();
      ::new (static_cast<void*>(value_stash)) T(**begin);
      return value_stash;
    } else {
      return static_cast<const T*>(std::addressof(**begin));
    }
  }
}

template <typename T, typename Iter>
const void* RangeManagerCreate(RangeStorage* range, Iter begin, Iter end) {
  CreateRangeStorage<T, Iter>(range);
  ::new (static_cast<void*>(StorageCast<Iter>(range->begin)))
      Iter(std::move(begin));
  ::new (static_cast<void*>(StorageCast<Iter>(range->end)))
      Iter(std::move(end));
  if constexpr (IsValueStashRequired<T, Iter>()) {
    auto* value_stash = StorageCast<T>(range->value_stash);
    ::new (static_cast<void*>(value_stash))
        T(**StorageCast<Iter>(range->begin));
    return value_stash;
  } else {
    return static_cast<const T*>(
        std::addressof(**StorageCast<Iter>(range->begin)));
  }
}

template <typename T, typename Iter>
const void* RangeManagerCopy(const RangeStorage* src, RangeStorage* dest) {
  CreateRangeStorage<T, Iter>(dest);
  ::new (static_cast<void*>(StorageCast<Iter>(dest->begin)))
      Iter(*StorageCast<Iter>(src->begin));
  ::new (static_cast<void*>(StorageCast<Iter>(dest->end)))
      Iter(*StorageCast<Iter>(src->end));
  if constexpr (IsValueStashRequired<T, Iter>()) {
    auto* value_stash = StorageCast<T>(dest->value_stash);
    ::new (static_cast<void*>(value_stash)) T(**StorageCast<Iter>(dest->begin));
    return value_stash;
  } else {
    return static_cast<const T*>(
        std::addressof(**StorageCast<Iter>(dest->begin)));
  }
}

template <typename T, typename Iter>
const void* RangeManagerMove(RangeStorage* src, RangeStorage* dest) {
  if constexpr (IsValueStashRequired<T, Iter>()) {
    if constexpr (IsStoredInline<T>()) {
      ::new (static_cast<void*>(&dest->value_stash.small[0]))
          T(std::move(*StorageCast<T>(src->value_stash)));
      StorageCast<T>(src->value_stash)->~T();
    } else {
      dest->value_stash.large = src->value_stash.large;
    }
  }
  if constexpr (IsStoredInline<Iter>()) {
    ::new (static_cast<void*>(&dest->begin.small[0]))
        Iter(std::move(*StorageCast<Iter>(src->begin)));
    ::new (static_cast<void*>(&dest->end.small[0]))
        Iter(std::move(*StorageCast<Iter>(src->end)));
    StorageCast<Iter>(src->end)->~Iter();
    StorageCast<Iter>(src->begin)->~Iter();
  } else {
    dest->begin.large = src->begin.large;
    dest->end.large = src->end.large;
  }
  if constexpr (IsValueStashRequired<T, Iter>()) {
    return StorageCast<T>(dest->value_stash);
  } else {
    return static_cast<const T*>(
        std::addressof(**StorageCast<Iter>(dest->begin)));
  }
}

template <typename T, typename Iter>
OperationOutput RangeManager(Operation op, OperationInput input) {
  OperationOutput output;
  switch (op) {
    case Operation::kCreate: {
      output.value = RangeManagerCreate<T, Iter>(
          input.create.storage,
          std::move(*static_cast<Iter*>(input.create.begin)),
          std::move(*static_cast<Iter*>(input.create.end)));
    } break;
    case Operation::kAdvanceOne: {
      output.value = RangeManagerAdvanceOne<T, Iter>(input.advance_one);
    } break;
    case Operation::kDestroy: {
      RangeManagerDestroy<T, Iter>(input.destroy);
      output.value = nullptr;
    } break;
    case Operation::kCopy: {
      output.value = RangeManagerCopy<T, Iter>(input.copy.src, input.copy.dest);
    } break;
    case Operation::kMove: {
      output.value = RangeManagerMove<T, Iter>(input.move.src, input.move.dest);
    } break;
  }
  return output;
}

template <typename T>
class Iterator final {
 public:
  using iterator_category = std::input_iterator_tag;
  using value_type = T;
  using pointer = const value_type*;
  using reference = const value_type&;
  using difference_type = ptrdiff_t;

  Iterator() = default;

  template <typename Iter>
  Iterator(Iter first, Iter last) {
    if (first != last) {
      manager_ = &RangeManager<T, Iter>;
      value_ = static_cast<pointer>(
          ((*manager_)(Operation::kCreate,
                       OperationInput{.create = {.storage = &range_,
                                                 .begin = std::addressof(first),
                                                 .end = std::addressof(last)}}))
              .value);
    }
  }

  Iterator(const Iterator& other) { Copy(other); }

  Iterator(Iterator&& other) noexcept { Move(other); }

  ~Iterator() { Destroy(); }

  Iterator& operator=(const Iterator& other) {
    if (ABSL_PREDICT_TRUE(this != std::addressof(other))) {
      Destroy();
      Copy(other);
    }
    return *this;
  }

  Iterator& operator=(Iterator&& other) noexcept {
    if (ABSL_PREDICT_TRUE(this != std::addressof(other))) {
      Destroy();
      Move(other);
    }
    return *this;
  }

  reference operator*() const {
    ABSL_DCHECK(value_ != nullptr) << "SizedInputIterator is at end";
    return *value_;
  }

  pointer operator->() const {
    ABSL_DCHECK(value_ != nullptr) << "SizedInputIterator is at end";
    return value_;
  }

  Iterator& operator++() {
    ABSL_DCHECK(value_ != nullptr) << "SizedInputIterator is at end";
    value_ = static_cast<pointer>(
        ((*manager_)(Operation::kAdvanceOne,
                     OperationInput{.advance_one = &range_}))
            .value);
    if (value_ == nullptr) {
      manager_ = nullptr;
    }
    return *this;
  }

  friend bool operator==(const Iterator& lhs, const Iterator& rhs) {
    ABSL_DCHECK(lhs.manager_ == rhs.manager_ || lhs.manager_ == nullptr ||
                rhs.manager_ == nullptr);
    ABSL_DCHECK(lhs.value_ == nullptr || rhs.value_ == nullptr ||
                lhs.value_ == rhs.value_);
    return lhs.value_ == rhs.value_;
  }

 private:
  void Destroy() noexcept {
    if (manager_ != nullptr) {
      (*manager_)(Operation::kDestroy, OperationInput{.destroy = &range_});
    }
  }

  void Copy(const Iterator& other) {
    manager_ = other.manager_;
    if (manager_ != nullptr) {
      value_ = static_cast<pointer>(
          ((*manager_)(
               Operation::kCopy,
               OperationInput{.copy = {.src = &other.range_, .dest = &range_}}))
              .value);
    } else {
      value_ = nullptr;
    }
  }

  void Move(Iterator& other) noexcept {
    manager_ = other.manager_;
    other.manager_ = nullptr;
    if (manager_ != nullptr) {
      value_ = static_cast<pointer>(
          ((*manager_)(
               Operation::kMove,
               OperationInput{.move = {.src = &other.range_, .dest = &range_}}))
              .value);
    } else {
      value_ = nullptr;
    }
  }

  pointer value_ = nullptr;
  RangeManagerFn manager_ = nullptr;
  RangeStorage range_;
};

template <typename T>
inline bool operator!=(const Iterator<T>& lhs, const Iterator<T>& rhs) {
  return !operator==(lhs, rhs);
}

}  // namespace sized_input_view_internal

template <typename T>
class SizedInputView final {
 public:
  using iterator = sized_input_view_internal::Iterator<T>;
  using const_iterator = iterator;
  using value_type = T;
  using reference = const value_type&;
  using const_reference = reference;
  using pointer = const value_type*;
  using const_pointer = pointer;
  using size_type = size_t;

  SizedInputView() = default;
  SizedInputView(const SizedInputView&) = default;
  SizedInputView& operator=(const SizedInputView&) = default;

  SizedInputView(SizedInputView&& other) noexcept
      : begin_(std::move(other.begin_)), size_(other.size_) {
    other.size_ = 0;
  }

  SizedInputView& operator=(SizedInputView&& other) noexcept {
    if (ABSL_PREDICT_TRUE(this != std::addressof(other))) {
      begin_ = std::move(other.begin_);
      size_ = other.size_;
      other.size_ = 0;
    }
    return *this;
  }

  template <typename C,
            typename IterType = sized_input_view_internal::ConstIterator<C>,
            typename SizeType = sized_input_view_internal::SizeType<C>,
            typename = std::enable_if_t<
                (sized_input_view_internal::CanIterateAsType<T, IterType>() &&
                 std::is_convertible_v<SizeType, size_type> &&
                 !std::is_same_v<SizedInputView<T>, C>)>>
  // NOLINTNEXTLINE(google-explicit-constructor)
  SizedInputView(const C& c ABSL_ATTRIBUTE_LIFETIME_BOUND)
      : begin_(std::begin(c), std::end(c)), size_(std::size(c)) {}

  // NOLINTNEXTLINE(google-explicit-constructor)
  SizedInputView(
      const std::initializer_list<T>& c ABSL_ATTRIBUTE_LIFETIME_BOUND)
      : begin_(c.begin(), c.end()), size_(c.size()) {}

  const iterator& begin() const ABSL_ATTRIBUTE_LIFETIME_BOUND { return begin_; }

  iterator end() const { return iterator(); }

  bool empty() const { return size() == 0; }

  size_type size() const { return size_; }

 private:
  iterator begin_;
  size_type size_ = 0;
};

}  // namespace cel

#endif  // THIRD_PARTY_CEL_CPP_COMMON_SIZED_INPUT_VIEW_H_
