// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <map>
#include <memory>
#include <string>
#include <utility>

#include <base/strings/stringprintf.h>
#include <base/strings/string_number_conversions.h>

#include "runtime_probe/probe_result_checker.h"
#include "runtime_probe/utils/type_utils.h"

namespace {
using ValidatorOperator = runtime_probe::ValidatorOperator;
using ReturnCode = runtime_probe::FieldConverter::ReturnCode;

constexpr const char* GetPrefix(ValidatorOperator op) {
  switch (op) {
    case ValidatorOperator::NOP:
      return "!nop ";
    case ValidatorOperator::RE:
      return "!re ";
    case ValidatorOperator::EQ:
      return "!eq ";
    case ValidatorOperator::NE:
      return "!ne ";
    case ValidatorOperator::GT:
      return "!gt ";
    case ValidatorOperator::GE:
      return "!ge ";
    case ValidatorOperator::LT:
      return "!lt ";
    case ValidatorOperator::LE:
      return "!le ";
    default:
      DCHECK(false) << "should never reach here";
  }
  return nullptr;
}

constexpr const char* ToString(ValidatorOperator op) {
  switch (op) {
    case ValidatorOperator::NOP:
      return "NOP";
    case ValidatorOperator::RE:
      return "RE";
    case ValidatorOperator::EQ:
      return "EQ";
    case ValidatorOperator::NE:
      return "NE";
    case ValidatorOperator::GT:
      return "GT";
    case ValidatorOperator::GE:
      return "GE";
    case ValidatorOperator::LT:
      return "LT";
    case ValidatorOperator::LE:
      return "LE";
    default:
      DCHECK(false) << "should never reach here";
  }
  return nullptr;
}

bool SplitValidateRuleString(const base::StringPiece& validate_rule,
                             ValidatorOperator* operator_,
                             base::StringPiece* operand) {
  if (validate_rule.empty()) {
    *operator_ = ValidatorOperator::NOP;
    *operand = "";
    return true;
  }

  auto first_space_idx = validate_rule.find_first_of(' ');
  auto prefix = validate_rule.substr(0, first_space_idx + 1);
  auto rest = validate_rule.substr(first_space_idx + 1);

  for (int i = 0; i < static_cast<int>(ValidatorOperator::NUM_OP); i++) {
    auto op = static_cast<ValidatorOperator>(i);
    if (prefix == GetPrefix(op)) {
      *operator_ = op;
      if (op != ValidatorOperator::NOP)  // NOP shouldn't have operand.
        *operand = rest;
      return true;
    }
  }
  return false;
}

template <typename ConverterType>
std::unique_ptr<ConverterType> BuildNumericConverter(
    const base::StringPiece& validate_rule) {
  ValidatorOperator op;
  base::StringPiece rest;

  if (SplitValidateRuleString(validate_rule, &op, &rest)) {
    if (op == ValidatorOperator::NOP)
      return std::make_unique<ConverterType>(op, 0);

    if (op == ValidatorOperator::EQ || op == ValidatorOperator::NE ||
        op == ValidatorOperator::GT || op == ValidatorOperator::GE ||
        op == ValidatorOperator::LT || op == ValidatorOperator::LE) {
      typename ConverterType::OperandType operand;
      if (ConverterType::StringToOperand(rest.as_string(), &operand)) {
        return std::make_unique<ConverterType>(op, operand);
      } else {
        LOG(ERROR) << "Can't convert to operand: " << rest.as_string();
      }
    }
  }
  LOG(ERROR) << "Invalid validate rule: " << validate_rule;
  return nullptr;
}

template <typename ValueType>
ReturnCode CheckNumber(ValidatorOperator op,
                       const ValueType lhs,
                       const ValueType rhs) {
  bool is_valid = true;
  switch (op) {
    case ValidatorOperator::NOP:
      break;
    case ValidatorOperator::EQ:
      is_valid = lhs == rhs;
      break;
    case ValidatorOperator::GE:
      is_valid = lhs >= rhs;
      break;
    case ValidatorOperator::GT:
      is_valid = lhs > rhs;
      break;
    case ValidatorOperator::LE:
      is_valid = lhs <= rhs;
      break;
    case ValidatorOperator::LT:
      is_valid = lhs < rhs;
      break;
    case ValidatorOperator::NE:
      is_valid = lhs != rhs;
      break;
    default:
      return ReturnCode::UNSUPPORTED_OPERATOR;
  }
  return is_valid ? ReturnCode::OK : ReturnCode::INVALID_VALUE;
}

}  // namespace

namespace runtime_probe {

using ReturnCode = FieldConverter::ReturnCode;

std::string StringFieldConverter::ToString() const {
  return base::StringPrintf("StringFieldConverter(%s, %s)",
                            ::ToString(operator_), operand_.c_str());
}

std::string IntegerFieldConverter::ToString() const {
  return base::StringPrintf("IntegerFieldConverter(%s, %d)",
                            ::ToString(operator_), operand_);
}

std::string HexFieldConverter::ToString() const {
  return base::StringPrintf("IntegerFieldConverter(%s, 0x%x)",
                            ::ToString(operator_), operand_);
}

std::string DoubleFieldConverter::ToString() const {
  return base::StringPrintf("IntegerFieldConverter(%s, %f)",
                            ::ToString(operator_), operand_);
}

std::unique_ptr<StringFieldConverter> StringFieldConverter::Build(
    const base::StringPiece& validate_rule) {
  ValidatorOperator op;
  base::StringPiece pattern;

  if (SplitValidateRuleString(validate_rule, &op, &pattern)) {
    if (op == ValidatorOperator::NOP)
      return std::make_unique<StringFieldConverter>(op, "");

    if (op == ValidatorOperator::EQ || op == ValidatorOperator::NE) {
      return std::make_unique<StringFieldConverter>(op, pattern);
    }

    if (op == ValidatorOperator::RE) {
      auto instance = std::make_unique<StringFieldConverter>(op, pattern);
      if (instance->regex_->error().empty()) {
        // No error, the pattern is valid.
        return instance;
      }
      // Error string is set to non-empty if there are errors.
      LOG(ERROR) << "Invalid pattern: " << pattern;
      LOG(ERROR) << instance->regex_->error();
    }
  }

  LOG(ERROR) << "Invalid validate rule: " << validate_rule;
  return nullptr;
}

std::unique_ptr<IntegerFieldConverter> IntegerFieldConverter::Build(
    const base::StringPiece& validate_rule) {
  return BuildNumericConverter<IntegerFieldConverter>(validate_rule);
}

std::unique_ptr<HexFieldConverter> HexFieldConverter::Build(
    const base::StringPiece& validate_rule) {
  return BuildNumericConverter<HexFieldConverter>(validate_rule);
}

std::unique_ptr<DoubleFieldConverter> DoubleFieldConverter::Build(
    const base::StringPiece& validate_rule) {
  return BuildNumericConverter<DoubleFieldConverter>(validate_rule);
}

ReturnCode StringFieldConverter::Convert(const std::string& field_name,
                                         base::Value* dict_value) const {
  CHECK(dict_value);

  auto* value = dict_value->FindKey(field_name);
  if (!value)
    return ReturnCode::FIELD_NOT_FOUND;

  switch (value->type()) {
    case base::Value::Type::DOUBLE:
      dict_value->SetStringKey(field_name, std::to_string(value->GetDouble()));
      return ReturnCode::OK;
    case base::Value::Type::INTEGER:
      dict_value->SetStringKey(field_name, std::to_string(value->GetInt()));
      return ReturnCode::OK;
    case base::Value::Type::NONE:
      dict_value->SetStringKey(field_name, "null");
      return ReturnCode::OK;
    case base::Value::Type::STRING:
      return ReturnCode::OK;
    default:
      return ReturnCode::INCOMPATIBLE_VALUE;
  }
}

ReturnCode IntegerFieldConverter::Convert(const std::string& field_name,
                                          base::Value* dict_value) const {
  CHECK(dict_value);

  auto* value = dict_value->FindKey(field_name);
  if (!value)
    return ReturnCode::FIELD_NOT_FOUND;

  switch (value->type()) {
    case base::Value::Type::DOUBLE:
      dict_value->SetIntKey(field_name, static_cast<int>(value->GetDouble()));
      return ReturnCode::OK;
    case base::Value::Type::INTEGER:
      return ReturnCode::OK;
    case base::Value::Type::STRING: {
      const auto& string_value = value->GetString();
      int int_value;
      if (StringToInt(string_value, &int_value)) {
        dict_value->SetIntKey(field_name, int_value);
        return ReturnCode::OK;
      } else {
        LOG(ERROR) << "Failed to convert '" << string_value << "' to integer.";
        return ReturnCode::INCOMPATIBLE_VALUE;
      }
    }
    default:
      return ReturnCode::INCOMPATIBLE_VALUE;
  }
}

ReturnCode HexFieldConverter::Convert(const std::string& field_name,
                                      base::Value* dict_value) const {
  CHECK(dict_value);

  auto* value = dict_value->FindKey(field_name);
  if (!value)
    return ReturnCode::FIELD_NOT_FOUND;

  switch (value->type()) {
    case base::Value::Type::DOUBLE:
      dict_value->SetIntKey(field_name, static_cast<int>(value->GetDouble()));
      return ReturnCode::OK;
    case base::Value::Type::INTEGER:
      return ReturnCode::OK;
    case base::Value::Type::STRING: {
      const auto& string_value = value->GetString();
      int int_value;
      if (HexStringToInt(string_value, &int_value)) {
        dict_value->SetIntKey(field_name, int_value);
        return ReturnCode::OK;
      } else {
        LOG(ERROR) << "Failed to convert '" << string_value << "' to integer.";
        return ReturnCode::INCOMPATIBLE_VALUE;
      }
    }
    default:
      return ReturnCode::INCOMPATIBLE_VALUE;
  }
}

ReturnCode DoubleFieldConverter::Convert(const std::string& field_name,
                                         base::Value* dict_value) const {
  CHECK(dict_value);

  auto* value = dict_value->FindKey(field_name);
  if (!value)
    return ReturnCode::FIELD_NOT_FOUND;

  switch (value->type()) {
    case base::Value::Type::DOUBLE:
      return ReturnCode::OK;
    case base::Value::Type::INTEGER:
      dict_value->SetDoubleKey(field_name, value->GetDouble());
      return ReturnCode::OK;
    case base::Value::Type::STRING: {
      const auto& string_value = value->GetString();
      double double_value;
      if (StringToDouble(string_value, &double_value)) {
        dict_value->SetDoubleKey(field_name, double_value);
        return ReturnCode::OK;
      } else {
        LOG(ERROR) << "Failed to convert '" << string_value << "' to double.";
        return ReturnCode::INCOMPATIBLE_VALUE;
      }
    }
    default:
      return ReturnCode::INCOMPATIBLE_VALUE;
  }
}

ReturnCode StringFieldConverter::Validate(const std::string& field_name,
                                          base::Value* dict_value) const {
  CHECK(dict_value);

  auto* value_ = dict_value->FindKey(field_name);
  if (!value_)
    return ReturnCode::FIELD_NOT_FOUND;
  if (!value_->is_string())
    return ReturnCode::INCOMPATIBLE_VALUE;

  const auto& value = value_->GetString();
  bool is_valid = true;
  switch (operator_) {
    case ValidatorOperator::NOP:
      break;
    case ValidatorOperator::EQ:
      is_valid = value == operand_;
      break;
    case ValidatorOperator::RE:
      is_valid = regex_->FullMatch(value);
      break;
    case ValidatorOperator::NE:
      is_valid = value != operand_;
      break;
    default:
      return ReturnCode::UNSUPPORTED_OPERATOR;
  }
  return is_valid ? ReturnCode::OK : ReturnCode::INVALID_VALUE;
}

ReturnCode IntegerFieldConverter::Validate(const std::string& field_name,
                                           base::Value* dict_value) const {
  CHECK(dict_value);

  auto* value_ = dict_value->FindKey(field_name);
  if (!value_)
    return ReturnCode::FIELD_NOT_FOUND;
  if (!value_->is_int())
    return ReturnCode::INCOMPATIBLE_VALUE;
  const auto value = value_->GetInt();

  return CheckNumber(operator_, value, operand_);
}

ReturnCode HexFieldConverter::Validate(const std::string& field_name,
                                       base::Value* dict_value) const {
  CHECK(dict_value);

  auto* value_ = dict_value->FindKey(field_name);
  if (!value_)
    return ReturnCode::FIELD_NOT_FOUND;
  if (!value_->is_int())
    return ReturnCode::INCOMPATIBLE_VALUE;
  const auto value = value_->GetInt();

  return CheckNumber(operator_, value, operand_);
}

ReturnCode DoubleFieldConverter::Validate(const std::string& field_name,
                                          base::Value* dict_value) const {
  CHECK(dict_value);

  auto* value_ = dict_value->FindKey(field_name);
  if (!value_)
    return ReturnCode::FIELD_NOT_FOUND;
  if (!value_->is_double() && !value_->is_int())
    return ReturnCode::INCOMPATIBLE_VALUE;
  const auto value = value_->GetDouble();

  return CheckNumber(operator_, value, operand_);
}

std::unique_ptr<ProbeResultChecker> ProbeResultChecker::FromValue(
    const base::Value& dict_value) {
  std::unique_ptr<ProbeResultChecker> instance{new ProbeResultChecker()};
  for (const auto& entry : dict_value.DictItems()) {
    const auto& key = entry.first;
    const auto& val = entry.second;
    auto print_error_and_return = [&val]() {
      LOG(ERROR) << "'expect' attribute should be a list whose values are"
                 << "[<required:bool>, <expected_type:string>, "
                 << "<optional_validate_rule:string>], got: " << val;
      return nullptr;
    };

    const auto& list_value = val.GetList();

    if (list_value.size() < 2 || list_value.size() > 3)
      return print_error_and_return();

    if (!list_value[0].is_bool())
      return print_error_and_return();
    bool required = list_value[0].GetBool();
    auto* target =
        required ? &instance->required_fields_ : &instance->optional_fields_;

    if (!list_value[1].is_string())
      return print_error_and_return();
    const auto& expect_type = list_value[1].GetString();

    std::string validate_rule;
    if (list_value.size() == 3) {
      if (!list_value[2].is_string())
        return print_error_and_return();
      validate_rule = list_value[2].GetString();
    }

    std::unique_ptr<FieldConverter> converter = nullptr;
    if (expect_type == "str") {
      converter = StringFieldConverter::Build(validate_rule);
    } else if (expect_type == "int") {
      converter = IntegerFieldConverter::Build(validate_rule);
    } else if (expect_type == "double") {
      converter = DoubleFieldConverter::Build(validate_rule);
    } else if (expect_type == "hex") {
      converter = HexFieldConverter::Build(validate_rule);
    }

    if (converter == nullptr) {
      LOG(ERROR) << "Cannot build converter, 'expect_type': " << expect_type
                 << ", 'validate_rule': " << validate_rule;
      return nullptr;
    } else {
      (*target)[key] = std::move(converter);
    }
  }

  return instance;
}

bool ProbeResultChecker::Apply(base::Value* probe_result) const {
  bool success = true;

  CHECK(probe_result != nullptr);

  // Try to convert and validate each required fields.
  // Any failures will cause the final result be |false|.
  for (const auto& entry : required_fields_) {
    const auto& key = entry.first;
    const auto& converter = entry.second;
    if (!probe_result->FindKey(key)) {
      LOG(ERROR) << "Missing key: " << key;
      success = false;
      break;
    }

    auto return_code = converter->Convert(key, probe_result);
    if (return_code != ReturnCode::OK) {
      auto* value = probe_result->FindKey(key);
      LOG(ERROR) << "Failed to apply " << converter->ToString() << " on "
                 << *value << "(ReturnCode = " << static_cast<int>(return_code)
                 << ")";

      success = false;
      break;
    }
  }

  // |ProbeStatement| will remove this element from final results, there is no
  // need to continue.
  if (!success) {
    VLOG(3) << "probe_result = " << *probe_result;
    return false;
  }

  // Try to convert and validate each optional fields.
  // For failures, just remove them from probe_result and continue.
  for (const auto& entry : optional_fields_) {
    const auto& key = entry.first;
    const auto& converter = entry.second;
    if (!probe_result->FindKey(key))
      continue;

    auto return_code = converter->Convert(key, probe_result);
    if (return_code != ReturnCode::OK) {
      VLOG(1) << "Optional field '" << key << "' has unexpected value, "
              << "remove it from probe result.";
      probe_result->RemoveKey(key);
    }
  }

  // Now all fields should have the correct type, let's validate them.
  for (const auto& entry : required_fields_) {
    auto return_code = entry.second->Validate(entry.first, probe_result);
    if (return_code != ReturnCode::OK) {
      success = false;
      break;
    }
  }
  // Optional fields shouldn't have expect value.

  return success;
}

}  // namespace runtime_probe
