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

// IWYU pragma: private, include "common/value.h"
// IWYU pragma: friend "common/value.h"

#ifndef THIRD_PARTY_CEL_CPP_COMMON_VALUES_PARSED_STRUCT_VALUE_H_
#define THIRD_PARTY_CEL_CPP_COMMON_VALUES_PARSED_STRUCT_VALUE_H_

#include <cstdint>
#include <ostream>
#include <string>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/nullability.h"
#include "absl/functional/function_ref.h"
#include "absl/log/absl_check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "base/attribute.h"
#include "common/native_type.h"
#include "common/type.h"
#include "common/value_kind.h"
#include "common/values/custom_value.h"
#include "common/values/values.h"
#include "runtime/runtime_options.h"
#include "google/protobuf/arena.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/io/zero_copy_stream.h"
#include "google/protobuf/message.h"

namespace cel {

class CustomStructValueInterface;
class CustomStructValue;
class Value;
struct CustomStructValueDispatcher;
using CustomStructValueContent = CustomValueContent;

struct CustomStructValueDispatcher {
  using GetTypeId = NativeTypeId (*)(
      absl::Nonnull<const CustomStructValueDispatcher*> dispatcher,
      CustomStructValueContent content);

  using GetArena = absl::Nullable<google::protobuf::Arena*> (*)(
      absl::Nonnull<const CustomStructValueDispatcher*> dispatcher,
      CustomStructValueContent content);

  using GetTypeName = absl::string_view (*)(
      absl::Nonnull<const CustomStructValueDispatcher*> dispatcher,
      CustomStructValueContent content);

  using DebugString = std::string (*)(
      absl::Nonnull<const CustomStructValueDispatcher*> dispatcher,
      CustomStructValueContent content);

  using GetRuntimeType = StructType (*)(
      absl::Nonnull<const CustomStructValueDispatcher*> dispatcher,
      CustomStructValueContent content);

  using SerializeTo = absl::Status (*)(
      absl::Nonnull<const CustomStructValueDispatcher*> dispatcher,
      CustomStructValueContent content,
      absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
      absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
      absl::Nonnull<google::protobuf::io::ZeroCopyOutputStream*> output);

  using ConvertToJsonObject = absl::Status (*)(
      absl::Nonnull<const CustomStructValueDispatcher*> dispatcher,
      CustomStructValueContent content,
      absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
      absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
      absl::Nonnull<google::protobuf::Message*> json);

  using Equal = absl::Status (*)(
      absl::Nonnull<const CustomStructValueDispatcher*> dispatcher,
      CustomStructValueContent content, const StructValue& other,
      absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
      absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
      absl::Nonnull<google::protobuf::Arena*> arena, absl::Nonnull<Value*> result);

  using IsZeroValue =
      bool (*)(absl::Nonnull<const CustomStructValueDispatcher*> dispatcher,
               CustomStructValueContent content);

  using GetFieldByName = absl::Status (*)(
      absl::Nonnull<const CustomStructValueDispatcher*> dispatcher,
      CustomStructValueContent content, absl::string_view name,
      ProtoWrapperTypeOptions unboxing_options,
      absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
      absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
      absl::Nonnull<google::protobuf::Arena*> arena, absl::Nonnull<Value*> result);

  using GetFieldByNumber = absl::Status (*)(
      absl::Nonnull<const CustomStructValueDispatcher*> dispatcher,
      CustomStructValueContent content, int64_t number,
      ProtoWrapperTypeOptions unboxing_options,
      absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
      absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
      absl::Nonnull<google::protobuf::Arena*> arena, absl::Nonnull<Value*> result);

  using HasFieldByName = absl::StatusOr<bool> (*)(
      absl::Nonnull<const CustomStructValueDispatcher*> dispatcher,
      CustomStructValueContent content, absl::string_view name);

  using HasFieldByNumber = absl::StatusOr<bool> (*)(
      absl::Nonnull<const CustomStructValueDispatcher*> dispatcher,
      CustomStructValueContent content, int64_t number);

  using ForEachField = absl::Status (*)(
      absl::Nonnull<const CustomStructValueDispatcher*> dispatcher,
      CustomStructValueContent content,
      absl::FunctionRef<absl::StatusOr<bool>(absl::string_view, const Value&)>
          callback,
      absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
      absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
      absl::Nonnull<google::protobuf::Arena*> arena);

  using Quality = absl::Status (*)(
      absl::Nonnull<const CustomStructValueDispatcher*> dispatcher,
      CustomStructValueContent content,
      absl::Span<const SelectQualifier> qualifiers, bool presence_test,
      absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
      absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
      absl::Nonnull<google::protobuf::Arena*> arena, absl::Nonnull<Value*> result,
      absl::Nonnull<int*> count);

  using Clone = CustomStructValue (*)(
      absl::Nonnull<const CustomStructValueDispatcher*> dispatcher,
      CustomStructValueContent content, absl::Nonnull<google::protobuf::Arena*> arena);

  absl::Nonnull<GetTypeId> get_type_id;

  absl::Nonnull<GetArena> get_arena;

  absl::Nonnull<GetTypeName> get_type_name;

  absl::Nullable<DebugString> debug_string = nullptr;

  absl::Nullable<GetRuntimeType> get_runtime_type = nullptr;

  absl::Nullable<SerializeTo> serialize_to = nullptr;

  absl::Nullable<ConvertToJsonObject> convert_to_json_object = nullptr;

  absl::Nullable<Equal> equal = nullptr;

  absl::Nonnull<IsZeroValue> is_zero_value;

  absl::Nonnull<GetFieldByName> get_field_by_name;

  absl::Nullable<GetFieldByNumber> get_field_by_number = nullptr;

  absl::Nonnull<HasFieldByName> has_field_by_name;

  absl::Nullable<HasFieldByNumber> has_field_by_number = nullptr;

  absl::Nonnull<ForEachField> for_each_field;

  absl::Nullable<Quality> qualify = nullptr;

  absl::Nonnull<Clone> clone;
};

class CustomStructValueInterface {
 public:
  CustomStructValueInterface() = default;
  CustomStructValueInterface(const CustomStructValueInterface&) = delete;
  CustomStructValueInterface(CustomStructValueInterface&&) = delete;

  virtual ~CustomStructValueInterface() = default;

  CustomStructValueInterface& operator=(const CustomStructValueInterface&) =
      delete;
  CustomStructValueInterface& operator=(CustomStructValueInterface&&) = delete;

  using ForEachFieldCallback =
      absl::FunctionRef<absl::StatusOr<bool>(absl::string_view, const Value&)>;

 private:
  friend class CustomStructValue;
  friend absl::Status common_internal::StructValueEqual(
      const CustomStructValueInterface& lhs, const StructValue& rhs,
      absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
      absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
      absl::Nonnull<google::protobuf::Arena*> arena, absl::Nonnull<Value*> result);

  virtual std::string DebugString() const = 0;

  virtual absl::Status SerializeTo(
      absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
      absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
      absl::Nonnull<google::protobuf::io::ZeroCopyOutputStream*> output) const = 0;

  virtual absl::Status ConvertToJsonObject(
      absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
      absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
      absl::Nonnull<google::protobuf::Message*> json) const = 0;

  virtual absl::string_view GetTypeName() const = 0;

  virtual StructType GetRuntimeType() const {
    return common_internal::MakeBasicStructType(GetTypeName());
  }

  virtual absl::Status Equal(
      const StructValue& other,
      absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
      absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
      absl::Nonnull<google::protobuf::Arena*> arena, absl::Nonnull<Value*> result) const;

  virtual bool IsZeroValue() const = 0;

  virtual absl::Status GetFieldByName(
      absl::string_view name, ProtoWrapperTypeOptions unboxing_options,
      absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
      absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
      absl::Nonnull<google::protobuf::Arena*> arena,
      absl::Nonnull<Value*> result) const = 0;

  virtual absl::Status GetFieldByNumber(
      int64_t number, ProtoWrapperTypeOptions unboxing_options,
      absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
      absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
      absl::Nonnull<google::protobuf::Arena*> arena,
      absl::Nonnull<Value*> result) const = 0;

  virtual absl::StatusOr<bool> HasFieldByName(absl::string_view name) const = 0;

  virtual absl::StatusOr<bool> HasFieldByNumber(int64_t number) const = 0;

  virtual absl::Status ForEachField(
      ForEachFieldCallback callback,
      absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
      absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
      absl::Nonnull<google::protobuf::Arena*> arena) const = 0;

  virtual absl::Status Qualify(
      absl::Span<const SelectQualifier> qualifiers, bool presence_test,
      absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
      absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
      absl::Nonnull<google::protobuf::Arena*> arena, absl::Nonnull<Value*> result,
      absl::Nonnull<int*> count) const;

  virtual CustomStructValue Clone(
      absl::Nonnull<google::protobuf::Arena*> arena) const = 0;

  virtual NativeTypeId GetNativeTypeId() const = 0;

  struct Content {
    absl::Nonnull<const CustomStructValueInterface*> interface;
    absl::Nonnull<google::protobuf::Arena*> arena;
  };
};

// Creates a custom struct value from a manual dispatch table `dispatcher` and
// opaque data `content` whose format is only know to functions in the manual
// dispatch table. The dispatch table should probably be valid for the lifetime
// of the process, but at a minimum must outlive all instances of the resulting
// value.
//
// IMPORTANT: This approach to implementing CustomStructValues should only be
// used when you know exactly what you are doing. When in doubt, just implement
// CustomStructValueInterface.
CustomStructValue UnsafeCustomStructValue(
    absl::Nonnull<const CustomStructValueDispatcher*> dispatcher
        ABSL_ATTRIBUTE_LIFETIME_BOUND,
    CustomStructValueContent content);

class CustomStructValue final
    : private common_internal::StructValueMixin<CustomStructValue> {
 public:
  static constexpr ValueKind kKind = ValueKind::kStruct;

  // Constructs a custom struct value from an implementation of
  // `CustomStructValueInterface` `interface` whose lifetime is tied to that of
  // the arena `arena`.
  CustomStructValue(absl::Nonnull<const CustomStructValueInterface*>
                        interface ABSL_ATTRIBUTE_LIFETIME_BOUND,
                    absl::Nonnull<google::protobuf::Arena*> arena
                        ABSL_ATTRIBUTE_LIFETIME_BOUND) {
    ABSL_DCHECK(interface != nullptr);
    ABSL_DCHECK(arena != nullptr);
    content_ =
        CustomStructValueContent::From(CustomStructValueInterface::Content{
            .interface = interface, .arena = arena});
  }

  CustomStructValue() = default;
  CustomStructValue(const CustomStructValue&) = default;
  CustomStructValue(CustomStructValue&&) = default;
  CustomStructValue& operator=(const CustomStructValue&) = default;
  CustomStructValue& operator=(CustomStructValue&&) = default;

  static constexpr ValueKind kind() { return kKind; }

  NativeTypeId GetTypeId() const;

  StructType GetRuntimeType() const;

  absl::string_view GetTypeName() const;

  std::string DebugString() const;

  // See Value::SerializeTo().
  absl::Status SerializeTo(
      absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
      absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
      absl::Nonnull<google::protobuf::io::ZeroCopyOutputStream*> output) const;

  // See Value::ConvertToJson().
  absl::Status ConvertToJson(
      absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
      absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
      absl::Nonnull<google::protobuf::Message*> json) const;

  // See Value::ConvertToJsonObject().
  absl::Status ConvertToJsonObject(
      absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
      absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
      absl::Nonnull<google::protobuf::Message*> json) const;

  absl::Status Equal(
      const Value& other,
      absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
      absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
      absl::Nonnull<google::protobuf::Arena*> arena, absl::Nonnull<Value*> result) const;
  using StructValueMixin::Equal;

  bool IsZeroValue() const;

  CustomStructValue Clone(absl::Nonnull<google::protobuf::Arena*> arena) const;

  absl::Status GetFieldByName(
      absl::string_view name, ProtoWrapperTypeOptions unboxing_options,
      absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
      absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
      absl::Nonnull<google::protobuf::Arena*> arena, absl::Nonnull<Value*> result) const;
  using StructValueMixin::GetFieldByName;

  absl::Status GetFieldByNumber(
      int64_t number, ProtoWrapperTypeOptions unboxing_options,
      absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
      absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
      absl::Nonnull<google::protobuf::Arena*> arena, absl::Nonnull<Value*> result) const;
  using StructValueMixin::GetFieldByNumber;

  absl::StatusOr<bool> HasFieldByName(absl::string_view name) const;

  absl::StatusOr<bool> HasFieldByNumber(int64_t number) const;

  using ForEachFieldCallback = CustomStructValueInterface::ForEachFieldCallback;

  absl::Status ForEachField(
      ForEachFieldCallback callback,
      absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
      absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
      absl::Nonnull<google::protobuf::Arena*> arena) const;

  absl::Status Qualify(
      absl::Span<const SelectQualifier> qualifiers, bool presence_test,
      absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
      absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
      absl::Nonnull<google::protobuf::Arena*> arena, absl::Nonnull<Value*> result,
      absl::Nonnull<int*> count) const;
  using StructValueMixin::Qualify;

  absl::Nullable<const CustomStructValueDispatcher*> dispatcher() const {
    return dispatcher_;
  }

  CustomStructValueContent content() const {
    ABSL_DCHECK(dispatcher_ != nullptr);
    return content_;
  }

  absl::Nullable<const CustomStructValueInterface*> interface() const {
    if (dispatcher_ == nullptr) {
      return content_.To<CustomStructValueInterface::Content>().interface;
    }
    return nullptr;
  }

  explicit operator bool() const {
    if (dispatcher_ == nullptr) {
      return content_.To<CustomStructValueInterface::Content>().interface !=
             nullptr;
    }
    return true;
  }

  friend void swap(CustomStructValue& lhs, CustomStructValue& rhs) noexcept {
    using std::swap;
    swap(lhs.dispatcher_, rhs.dispatcher_);
    swap(lhs.content_, rhs.content_);
  }

 private:
  friend class common_internal::ValueMixin<CustomStructValue>;
  friend class common_internal::StructValueMixin<CustomStructValue>;
  friend CustomStructValue UnsafeCustomStructValue(
      absl::Nonnull<const CustomStructValueDispatcher*> dispatcher
          ABSL_ATTRIBUTE_LIFETIME_BOUND,
      CustomStructValueContent content);

  // Constructs a custom struct value from a dispatcher and content. Only
  // accessible from `UnsafeCustomStructValue`.
  CustomStructValue(absl::Nonnull<const CustomStructValueDispatcher*> dispatcher
                        ABSL_ATTRIBUTE_LIFETIME_BOUND,
                    CustomStructValueContent content)
      : dispatcher_(dispatcher), content_(content) {
    ABSL_DCHECK(dispatcher != nullptr);
    ABSL_DCHECK(dispatcher->get_type_id != nullptr);
    ABSL_DCHECK(dispatcher->get_arena != nullptr);
    ABSL_DCHECK(dispatcher->get_type_name != nullptr);
    ABSL_DCHECK(dispatcher->is_zero_value != nullptr);
    ABSL_DCHECK(dispatcher->get_field_by_name != nullptr);
    ABSL_DCHECK(dispatcher->has_field_by_name != nullptr);
    ABSL_DCHECK(dispatcher->for_each_field != nullptr);
    ABSL_DCHECK(dispatcher->clone != nullptr);
  }

  absl::Nullable<const CustomStructValueDispatcher*> dispatcher_ = nullptr;
  CustomStructValueContent content_ = CustomStructValueContent::Zero();
};

inline std::ostream& operator<<(std::ostream& out,
                                const CustomStructValue& value) {
  return out << value.DebugString();
}

template <>
struct NativeTypeTraits<CustomStructValue> final {
  static NativeTypeId Id(const CustomStructValue& type) {
    return type.GetTypeId();
  }
};

inline CustomStructValue UnsafeCustomStructValue(
    absl::Nonnull<const CustomStructValueDispatcher*> dispatcher
        ABSL_ATTRIBUTE_LIFETIME_BOUND,
    CustomStructValueContent content) {
  return CustomStructValue(dispatcher, content);
}

}  // namespace cel

#endif  // THIRD_PARTY_CEL_CPP_COMMON_VALUES_PARSED_STRUCT_VALUE_H_
