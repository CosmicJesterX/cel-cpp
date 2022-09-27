// Copyright 2022 Google LLC
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

#ifndef THIRD_PARTY_CEL_CPP_EVAL_INTERNAL_INTEROP_H_
#define THIRD_PARTY_CEL_CPP_EVAL_INTERNAL_INTEROP_H_

#include <functional>
#include <memory>
#include <string>

#include "absl/base/attributes.h"
#include "absl/status/statusor.h"
#include "absl/types/variant.h"
#include "base/value.h"
#include "base/value_factory.h"
#include "eval/public/cel_value.h"
#include "internal/rtti.h"

namespace google::api::expr::runtime {
class UnknownSet;
}

namespace cel::interop_internal {

struct CelListAccess final {
  static internal::TypeInfo TypeId(
      const google::api::expr::runtime::CelList& list);
};

struct CelMapAccess final {
  static internal::TypeInfo TypeId(
      const google::api::expr::runtime::CelMap& map);
};

// Unlike ValueFactory::CreateStringValue, this does not copy input and instead
// wraps it. It should only be used for interop with the legacy CelValue.
absl::StatusOr<Persistent<StringValue>> CreateStringValueFromView(
    cel::ValueFactory& value_factory, absl::string_view input);

// Unlike ValueFactory::CreateBytesValue, this does not copy input and instead
// wraps it. It should only be used for interop with the legacy CelValue.
absl::StatusOr<Persistent<BytesValue>> CreateBytesValueFromView(
    cel::ValueFactory& value_factory, absl::string_view input);

base_internal::StringValueRep GetStringValueRep(
    const Persistent<StringValue>& value);

base_internal::BytesValueRep GetBytesValueRep(
    const Persistent<BytesValue>& value);

// Converts a legacy CEL value to the new CEL value representation.
absl::StatusOr<Persistent<Value>> FromLegacyValue(
    cel::ValueFactory& value_factory ABSL_ATTRIBUTE_LIFETIME_BOUND,
    const google::api::expr::runtime::CelValue& legacy_value);

// Converts a new CEL value to the legacy CEL value representation.
absl::StatusOr<google::api::expr::runtime::CelValue> ToLegacyValue(
    cel::ValueFactory& value_factory ABSL_ATTRIBUTE_LIFETIME_BOUND,
    const Persistent<Value>& value);

std::shared_ptr<base_internal::UnknownSetImpl> GetUnknownValueImpl(
    const Persistent<UnknownValue>& value);

std::shared_ptr<base_internal::UnknownSetImpl> GetUnknownSetImpl(
    const google::api::expr::runtime::UnknownSet& unknown_set);

void SetUnknownValueImpl(Persistent<UnknownValue>& value,
                         std::shared_ptr<base_internal::UnknownSetImpl> impl);

void SetUnknownSetImpl(google::api::expr::runtime::UnknownSet& unknown_set,
                       std::shared_ptr<base_internal::UnknownSetImpl> impl);

}  // namespace cel::interop_internal

#endif  // THIRD_PARTY_CEL_CPP_EVAL_INTERNAL_INTEROP_H_
