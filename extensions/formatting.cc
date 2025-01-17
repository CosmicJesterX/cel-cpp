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

#include "extensions/formatting.h"

#include <cmath>
#include <cstdint>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/container/btree_map.h"
#include "absl/memory/memory.h"
#include "absl/numeric/bits.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/escaping.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "common/value.h"
#include "common/value_kind.h"
#include "common/value_manager.h"
#include "internal/status_macros.h"
#include "runtime/function_adapter.h"
#include "runtime/function_registry.h"
#include "runtime/runtime_options.h"
#include "unicode/decimfmt.h"
#include "unicode/errorcode.h"
#include "unicode/locid.h"
#include "unicode/numfmt.h"

namespace cel::extensions {

namespace {

absl::StatusOr<absl::string_view> FormatString(
    ValueManager& value_manager, const Value& value,
    std::string& scratch ABSL_ATTRIBUTE_LIFETIME_BOUND);

absl::StatusOr<absl::string_view> FormatFixed(
    const Value& value, std::optional<int> precision, const icu::Locale& locale,
    std::string& scratch ABSL_ATTRIBUTE_LIFETIME_BOUND);

absl::StatusOr<std::pair<int64_t, std::optional<int>>> ParsePrecision(
    absl::string_view format) {
  if (format[0] != '.') return std::pair{0, std::nullopt};

  int64_t i = 1;
  while (i < std::ssize(format) && absl::ascii_isdigit(format[i])) {
    ++i;
  }
  if (i == std::ssize(format)) {
    return absl::InvalidArgumentError(
        "Unable to find end of precision specifier");
  }
  int precision;
  if (!absl::SimpleAtoi(format.substr(1, i - 1), &precision)) {
    return absl::InvalidArgumentError(
        "Unable to convert precision specifier to integer");
  }
  return std::pair{i, precision};
}

absl::StatusOr<std::unique_ptr<icu::NumberFormat>> CreateDoubleNumberFormater(
    std::optional<int> min_precision, std::optional<int> max_precision,
    bool use_scientific_notation, const icu::Locale& locale) {
  icu::ErrorCode error_code;  // NOLINT
  auto formatter =
      absl::WrapUnique(icu::NumberFormat::createInstance(locale, error_code));
  if (formatter == nullptr || error_code.isFailure()) {
    return absl::InternalError(
        absl::StrCat("Failed to create localized number formatter: ",
                     error_code.errorName()));
  }
  formatter->setMinimumIntegerDigits(1);
  static constexpr int kDefaultPrecision = 6;
  formatter->setMinimumFractionDigits(
      min_precision.value_or(kDefaultPrecision));
  formatter->setMaximumFractionDigits(
      max_precision.value_or(kDefaultPrecision));

  if (use_scientific_notation) {
    auto dec_fmt = static_cast<icu::DecimalFormat*>(formatter.get());
    dec_fmt->setExponentSignAlwaysShown(true);
    dec_fmt->setMinimumExponentDigits(2);
  }
  return formatter;
}

absl::StatusOr<absl::string_view> FormatDouble(
    double value, std::optional<int> min_precision,
    std::optional<int> max_precision, bool use_scientific_notation,
    absl::string_view unit, const icu::Locale& locale,
    std::string& scratch ABSL_ATTRIBUTE_LIFETIME_BOUND) {
  CEL_ASSIGN_OR_RETURN(auto formatter, CreateDoubleNumberFormater(
                                           min_precision, max_precision,
                                           use_scientific_notation, locale));
  icu::ErrorCode error_code;  // NOLINT
  icu::UnicodeString output;
  formatter->format(value, output, error_code);

  if (error_code.isSuccess()) {
    scratch.clear();
    output.toUTF8String(scratch);
    absl::StrAppend(&scratch, unit);
    return scratch;
  } else {
    return absl::InternalError(absl::StrCat("Failed to format fixed number: ",
                                            error_code.errorName()));
  }
}

void StrAppendQuoted(ValueKind kind, absl::string_view value,
                     std::string& target) {
  switch (kind) {
    case ValueKind::kBytes:
      target.push_back('b');
      [[fallthrough]];
    case ValueKind::kString:
      target.push_back('\"');
      for (char c : value) {
        if (c == '\\' || c == '\"') {
          target.push_back('\\');
        }
        target.push_back(c);
      }
      target.push_back('\"');
      break;
    case ValueKind::kTimestamp:
      absl::StrAppend(&target, "timestamp(\"", value, "\")");
      break;
    case ValueKind::kDuration:
      absl::StrAppend(&target, "duration(\"", value, "\")");
      break;
    case ValueKind::kDouble:
      if (value == "NaN") {
        absl::StrAppend(&target, "\"NaN\"");
      } else if (value == "+Inf") {
        absl::StrAppend(&target, "\"+Inf\"");
      } else if (value == "-Inf") {
        absl::StrAppend(&target, "\"-Inf\"");
      } else {
        absl::StrAppend(&target, value);
      }
      break;
    default:
      absl::StrAppend(&target, value);
      break;
  }
}

absl::StatusOr<absl::string_view> FormatList(
    ValueManager& value_manager, const Value& value,
    std::string& scratch ABSL_ATTRIBUTE_LIFETIME_BOUND) {
  CEL_ASSIGN_OR_RETURN(auto it, value.GetList().NewIterator(value_manager));
  scratch.clear();
  scratch.push_back('[');
  std::string value_scratch;

  while (it->HasNext()) {
    CEL_ASSIGN_OR_RETURN(auto next, it->Next(value_manager));
    absl::string_view next_str;
    CEL_ASSIGN_OR_RETURN(next_str,
                         FormatString(value_manager, next, value_scratch));
    StrAppendQuoted(next.kind(), next_str, scratch);
    absl::StrAppend(&scratch, ", ");
  }
  if (scratch.size() > 1) {
    scratch.resize(scratch.size() - 2);
  }
  scratch.push_back(']');
  return scratch;
}

absl::StatusOr<absl::string_view> FormatMap(
    ValueManager& value_manager, const Value& value,
    std::string& scratch ABSL_ATTRIBUTE_LIFETIME_BOUND) {
  absl::btree_map<std::string, Value> value_map;
  std::string value_scratch;
  CEL_RETURN_IF_ERROR(value.GetMap().ForEach(
      value_manager,
      [&](const Value& key, const Value& value) -> absl::StatusOr<bool> {
        if (key.kind() != ValueKind::kString &&
            key.kind() != ValueKind::kBool && key.kind() != ValueKind::kInt &&
            key.kind() != ValueKind::kUint) {
          return absl::InvalidArgumentError(
              absl::StrCat("Map keys must be strings, booleans, integers, or "
                           "unsigned integers, was given ",
                           key.GetTypeName()));
        }
        CEL_ASSIGN_OR_RETURN(auto key_str,
                             FormatString(value_manager, key, value_scratch));
        std::string quoted_key_str;
        StrAppendQuoted(key.kind(), key_str, quoted_key_str);
        value_map.emplace(std::move(quoted_key_str), value);
        return true;
      }));

  scratch.clear();
  scratch.push_back('{');
  for (const auto& [key, value] : value_map) {
    CEL_ASSIGN_OR_RETURN(auto value_str,
                         FormatString(value_manager, value, value_scratch));
    absl::StrAppend(&scratch, key, ":");
    StrAppendQuoted(value.kind(), value_str, scratch);
    absl::StrAppend(&scratch, ", ");
  }
  if (scratch.size() > 1) {
    scratch.resize(scratch.size() - 2);
  }
  scratch.push_back('}');
  return scratch;
}

absl::StatusOr<absl::string_view> FormatString(
    ValueManager& value_manager, const Value& value,
    std::string& scratch ABSL_ATTRIBUTE_LIFETIME_BOUND) {
  switch (value.kind()) {
    case ValueKind::kList:
      return FormatList(value_manager, value, scratch);
    case ValueKind::kMap:
      return FormatMap(value_manager, value, scratch);
    case ValueKind::kString:
      return value.GetString().NativeString(scratch);
    case ValueKind::kBytes:
      return value.GetBytes().NativeString(scratch);
    case ValueKind::kNull:
      return "null";
    case ValueKind::kInt:
      scratch.clear();
      absl::StrAppend(&scratch, value.GetInt().NativeValue());
      return scratch;
    case ValueKind::kUint:
      scratch.clear();
      absl::StrAppend(&scratch, value.GetUint().NativeValue());
      return scratch;
    case ValueKind::kDouble: {
      auto number = value.GetDouble().NativeValue();
      if (std::isnan(number)) {
        return "NaN";
      }
      if (number == std::numeric_limits<double>::infinity()) {
        return "+Inf";
      }
      if (number == -std::numeric_limits<double>::infinity()) {
        return "-Inf";
      }
      scratch.clear();
      absl::StrAppend(&scratch, number);
      return scratch;
    }
    case ValueKind::kTimestamp:
      scratch.clear();
      absl::StrAppend(&scratch, value.DebugString());
      return scratch;
    case ValueKind::kDuration:
      return FormatDouble(absl::ToDoubleSeconds(value.GetDuration()),
                          /*min_precision=*/0, /*max_precision=*/9,
                          /*use_scientific_notation=*/false,
                          /*unit=*/"s", icu::Locale::getDefault(), scratch);
    case ValueKind::kBool:
      if (value.GetBool().NativeValue()) {
        return "true";
      }
      return "false";
    case ValueKind::kType:
      return value.GetType().name();
    default:
      return absl::InvalidArgumentError(absl::StrFormat(
          "Could not convert argument %s to string", value.GetTypeName()));
  }
}

absl::StatusOr<absl::string_view> FormatDecimal(
    const Value& value, std::string& scratch ABSL_ATTRIBUTE_LIFETIME_BOUND) {
  scratch.clear();
  switch (value.kind()) {
    case ValueKind::kInt:
      absl::StrAppend(&scratch, value.GetInt().NativeValue());
      return scratch;
    case ValueKind::kUint:
      absl::StrAppend(&scratch, value.GetUint().NativeValue());
      return scratch;
    default:
      return absl::InvalidArgumentError(absl::StrCat(
          "Decimal clause can only be used on integers, was given ",
          value.GetTypeName()));
  }
}

absl::StatusOr<absl::string_view> FormatBinary(
    const Value& value, std::string& scratch ABSL_ATTRIBUTE_LIFETIME_BOUND) {
  decltype(value.GetUint().NativeValue()) unsigned_value;
  bool sign_bit = false;
  switch (value.kind()) {
    case ValueKind::kInt: {
      auto tmp = value.GetInt().NativeValue();
      if (tmp < 0) {
        sign_bit = true;
        // Negating min int is undefined behavior, so we need to use unsigned
        // arithmetic.
        using unsigned_type = std::make_unsigned<decltype(tmp)>::type;
        unsigned_value = -static_cast<unsigned_type>(tmp);
      } else {
        unsigned_value = tmp;
      }
      break;
    }
    case ValueKind::kUint:
      unsigned_value = value.GetUint().NativeValue();
      break;
    case ValueKind::kBool:
      if (value.GetBool().NativeValue()) {
        return "1";
      }
      return "0";
    default:
      return absl::InvalidArgumentError(absl::StrCat(
          "Binary clause can only be used on integers and bools, was given ",
          value.GetTypeName()));
  }

  if (unsigned_value == 0) {
    return "0";
  }

  int size = absl::bit_width(unsigned_value) + sign_bit;
  scratch.resize(size);
  for (int i = size - 1; i >= 0; --i) {
    if (unsigned_value & 1) {
      scratch[i] = '1';
    } else {
      scratch[i] = '0';
    }
    unsigned_value >>= 1;
  }
  if (sign_bit) {
    scratch[0] = '-';
  }
  return scratch;
}

absl::StatusOr<absl::string_view> FormatHex(
    const Value& value, bool use_upper_case,
    std::string& scratch ABSL_ATTRIBUTE_LIFETIME_BOUND) {
  switch (value.kind()) {
    case ValueKind::kString:
      scratch = absl::BytesToHexString(value.GetString().NativeString(scratch));
      break;
    case ValueKind::kBytes:
      scratch = absl::BytesToHexString(value.GetBytes().NativeString(scratch));
      break;
    case ValueKind::kInt: {
      // Golang supports signed hex, but absl::StrFormat does not. To be
      // compatible, we need to add a leading '-' if the value is negative.
      auto tmp = value.GetInt().NativeValue();
      if (tmp < 0) {
        // Negating min int is undefined behavior, so we need to use unsigned
        // arithmetic.
        using unsigned_type = std::make_unsigned<decltype(tmp)>::type;
        scratch = absl::StrFormat("-%x", -static_cast<unsigned_type>(tmp));
      } else {
        scratch = absl::StrFormat("%x", tmp);
      }
      break;
    }
    case ValueKind::kUint:
      scratch = absl::StrFormat("%x", value.GetUint().NativeValue());
      break;
    default:
      return absl::InvalidArgumentError(
          absl::StrCat("Hex clause can only be used on integers, byte buffers, "
                       "and strings, was given ",
                       value.GetTypeName()));
  }
  if (use_upper_case) {
    absl::AsciiStrToUpper(&scratch);
  }
  return scratch;
}

absl::StatusOr<absl::string_view> FormatOctal(
    const Value& value, std::string& scratch ABSL_ATTRIBUTE_LIFETIME_BOUND) {
  switch (value.kind()) {
    case ValueKind::kInt: {
      // Golang supports signed octals, but absl::StrFormat does not. To be
      // compatible, we need to add a leading '-' if the value is negative.
      auto tmp = value.GetInt().NativeValue();
      if (tmp < 0) {
        // Negating min int is undefined behavior, so we need to use unsigned
        // arithmetic.
        using unsigned_type = std::make_unsigned<decltype(tmp)>::type;
        scratch = absl::StrFormat("-%o", -static_cast<unsigned_type>(tmp));
      } else {
        scratch = absl::StrFormat("%o", tmp);
      }
      return scratch;
    }
    case ValueKind::kUint:
      scratch = absl::StrFormat("%o", value.GetUint().NativeValue());
      return scratch;
    default:
      return absl::InvalidArgumentError(
          absl::StrCat("Octal clause can only be used on integers, was given ",
                       value.GetTypeName()));
  }
}

absl::StatusOr<double> GetDouble(const Value& value, std::string& scratch) {
  if (value.kind() == ValueKind::kString) {
    auto str = value.GetString().NativeString(scratch);
    if (str == "NaN") {
      return std::nan("");
    } else if (str == "Infinity") {
      return std::numeric_limits<double>::infinity();
    } else if (str == "-Infinity") {
      return -std::numeric_limits<double>::infinity();
    } else {
      return absl::InvalidArgumentError(
          absl::StrCat("Only \"NaN\", \"Infinity\", and \"-Infinity\" are "
                       "supported for conversion to double: ",
                       str));
    }
  }
  if (value.kind() != ValueKind::kDouble) {
    return absl::InvalidArgumentError(
        absl::StrCat("Expected a double but got a ", value.GetTypeName()));
  }
  return value.GetDouble().NativeValue();
}

absl::StatusOr<absl::string_view> FormatFixed(
    const Value& value, std::optional<int> precision, const icu::Locale& locale,
    std::string& scratch ABSL_ATTRIBUTE_LIFETIME_BOUND) {
  CEL_ASSIGN_OR_RETURN(auto number, GetDouble(value, scratch));
  return FormatDouble(number, precision, precision,
                      /*use_scientific_notation=*/false, /*unit=*/"", locale,
                      scratch);
}

absl::StatusOr<absl::string_view> FormatScientific(
    const Value& value, std::optional<int> precision, const icu::Locale& locale,
    std::string& scratch ABSL_ATTRIBUTE_LIFETIME_BOUND) {
  CEL_ASSIGN_OR_RETURN(auto number, GetDouble(value, scratch));
  return FormatDouble(number, precision, precision,
                      /*use_scientific_notation=*/true, /*unit=*/"", locale,
                      scratch);
}

absl::StatusOr<std::pair<int64_t, absl::string_view>> ParseAndFormatClause(
    ValueManager& value_manager, absl::string_view format, const Value& value,
    const icu::Locale& locale,
    std::string& scratch ABSL_ATTRIBUTE_LIFETIME_BOUND) {
  CEL_ASSIGN_OR_RETURN(auto precision_pair, ParsePrecision(format));
  auto [read, precision] = precision_pair;
  switch (format[read]) {
    case 's': {
      CEL_ASSIGN_OR_RETURN(auto result,
                           FormatString(value_manager, value, scratch));
      return std::pair{read, result};
    }
    case 'd': {
      CEL_ASSIGN_OR_RETURN(auto result, FormatDecimal(value, scratch));
      return std::pair{read, result};
    }
    case 'f': {
      CEL_ASSIGN_OR_RETURN(auto result,
                           FormatFixed(value, precision, locale, scratch));
      return std::pair{read, result};
    }
    case 'e': {
      CEL_ASSIGN_OR_RETURN(auto result,
                           FormatScientific(value, precision, locale, scratch));
      return std::pair{read, result};
    }
    case 'b': {
      CEL_ASSIGN_OR_RETURN(auto result, FormatBinary(value, scratch));
      return std::pair{read, result};
    }
    case 'x':
    case 'X': {
      CEL_ASSIGN_OR_RETURN(
          auto result,
          FormatHex(value,
                    /*use_upper_case=*/format[read] == 'X', scratch));
      return std::pair{read, result};
    }
    case 'o': {
      CEL_ASSIGN_OR_RETURN(auto result, FormatOctal(value, scratch));
      return std::pair{read, result};
    }
    default:
      return absl::InvalidArgumentError(absl::StrFormat(
          "Unrecognized formatting clause \"%c\"", format[read]));
  }
}

absl::StatusOr<Value> Format(ValueManager& value_manager,
                             const StringValue& format_value,
                             const ListValue& args, const icu::Locale& locale) {
  std::string format_scratch, clause_scratch;
  absl::string_view format = format_value.NativeString(format_scratch);
  std::string result;
  result.reserve(format.size());
  int64_t arg_index = 0;
  CEL_ASSIGN_OR_RETURN(int64_t args_size, args.Size());
  for (int64_t i = 0; i < std::ssize(format); ++i) {
    if (format[i] != '%') {
      result.push_back(format[i]);
      continue;
    }
    ++i;
    if (i >= std::ssize(format)) {
      return absl::InvalidArgumentError("Unexpected end of format string");
    }
    if (format[i] == '%') {
      result.push_back('%');
      continue;
    }
    if (arg_index >= args_size) {
      return absl::InvalidArgumentError(
          absl::StrFormat("Index %d out of range", arg_index));
    }
    CEL_ASSIGN_OR_RETURN(auto value, args.Get(value_manager, arg_index++));
    CEL_ASSIGN_OR_RETURN(auto clause,
                         ParseAndFormatClause(value_manager, format.substr(i),
                                              value, locale, clause_scratch));
    absl::StrAppend(&result, clause.second);
    i += clause.first;
  }
  return value_manager.CreateUncheckedStringValue(std::move(result));
}

}  // namespace

absl::Status RegisterStringFormattingFunctions(FunctionRegistry& registry,
                                               const RuntimeOptions& options) {
  auto locale = icu::Locale::createCanonical(options.locale.c_str());
  if (locale.isBogus() || absl::string_view(locale.getISO3Language()).empty()) {
    return absl::InvalidArgumentError(
        absl::StrCat("Failed to parse locale: ", options.locale));
  }
  CEL_RETURN_IF_ERROR(registry.Register(
      BinaryFunctionAdapter<absl::StatusOr<Value>, StringValue, ListValue>::
          CreateDescriptor("format", /*receiver_style=*/true),
      BinaryFunctionAdapter<absl::StatusOr<Value>, StringValue, ListValue>::
          WrapFunction([locale](ValueManager& value_manager,
                                const StringValue& format,
                                const ListValue& args) {
            return Format(value_manager, format, args, locale);
          })));
  return absl::OkStatus();
}

}  // namespace cel::extensions
