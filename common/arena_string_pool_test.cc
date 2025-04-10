// Copyright 2024 Google LLC
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

#include "common/arena_string_pool.h"

#include <string>

#include "absl/strings/cord_test_helpers.h"
#include "absl/strings/string_view.h"
#include "internal/testing.h"
#include "google/protobuf/arena.h"

namespace cel {
namespace {

TEST(ArenaStringPool, InternCString) {
  google::protobuf::Arena arena;
  auto string_pool = NewArenaStringPool(&arena);
  auto expected = string_pool->InternString("Hello World!");
  auto got = string_pool->InternString("Hello World!");
  EXPECT_EQ(expected.data(), got.data());
}

TEST(ArenaStringPool, InternStringView) {
  google::protobuf::Arena arena;
  auto string_pool = NewArenaStringPool(&arena);
  auto expected = string_pool->InternString(absl::string_view("Hello World!"));
  auto got = string_pool->InternString("Hello World!");
  EXPECT_EQ(expected.data(), got.data());
}

TEST(ArenaStringPool, InternStringSmall) {
  google::protobuf::Arena arena;
  auto string_pool = NewArenaStringPool(&arena);
  auto expected = string_pool->InternString(std::string("Hello World!"));
  auto got = string_pool->InternString("Hello World!");
  EXPECT_EQ(expected.data(), got.data());
}

TEST(ArenaStringPool, InternStringLarge) {
  google::protobuf::Arena arena;
  auto string_pool = NewArenaStringPool(&arena);
  auto expected = string_pool->InternString(
      std::string("This string is larger than std::string itself!"));
  auto got = string_pool->InternString(
      "This string is larger than std::string itself!");
  EXPECT_EQ(expected.data(), got.data());
}

TEST(ArenaStringPool, InternCord) {
  google::protobuf::Arena arena;
  auto string_pool = NewArenaStringPool(&arena);
  auto expected = string_pool->InternString(absl::MakeFragmentedCord(
      {"This string is larger", " ", "than absl::Cord itself!"}));
  auto got = string_pool->InternString(
      "This string is larger than absl::Cord itself!");
  EXPECT_EQ(expected.data(), got.data());
}

}  // namespace
}  // namespace cel
