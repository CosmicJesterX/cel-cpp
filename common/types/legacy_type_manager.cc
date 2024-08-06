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

#include "common/types/legacy_type_manager.h"

#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "common/type.h"

namespace cel::common_internal {

ListType LegacyTypeManager::CreateListTypeImpl(const Type& element) {
  return ListType(GetMemoryManager(), Type(element));
}

MapType LegacyTypeManager::CreateMapTypeImpl(const Type& key,
                                             const Type& value) {
  return MapType(GetMemoryManager(), Type(key), Type(value));
}

OpaqueType LegacyTypeManager::CreateOpaqueTypeImpl(
    absl::string_view name, absl::Span<const Type> parameters) {
  return OpaqueType(GetMemoryManager(), name, parameters);
}

}  // namespace cel::common_internal
