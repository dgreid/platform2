// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "buffet/commands/schema_constants.h"

namespace buffet {

namespace errors {
namespace commands {
const char kDomain[] = "command_schema";

const char kOutOfRange[] = "out_of_range";
const char kTypeMismatch[] = "type_mismatch";
const char kPropTypeChanged[] = "param_type_changed";
const char kUnknownType[] = "unknown_type";
const char kInvalidPropDef[] = "invalid_parameter_definition";
const char kInvalidPropValue[] = "invalid_parameter_value";
const char kNoTypeInfo[] = "no_type_info";
const char kPropertyMissing[] = "parameter_missing";
const char kUnknownProperty[] = "unexpected_parameter";
const char kInvalidObjectSchema[] = "invalid_object_schema";
const char kDuplicateCommandDef[] = "duplicate_command_definition";
const char kInvalidCommandName[] = "invalid_command_name";
const char kCommandFailed[] = "command_failed";
const char kInvalidCommandVisibility[] = "invalid_command_visibility";
const char kInvalidMinimalRole[] = "invalid_minimal_role";
}  // namespace commands
}  // namespace errors

namespace commands {
namespace attributes {

const char kType[] = "type";
const char kDisplayName[] = "displayName";
const char kDefault[] = "default";
const char kItems[] = "items";

const char kNumeric_Min[] = "minimum";
const char kNumeric_Max[] = "maximum";

const char kString_MinLength[] = "minLength";
const char kString_MaxLength[] = "maxLength";

const char kOneOf_Enum[] = "enum";
const char kOneOf_Metadata[] = "metadata";

const char kObject_Properties[] = "properties";
const char kObject_AdditionalProperties[] = "additionalProperties";

const char kCommand_Id[] = "id";
const char kCommand_Name[] = "name";
const char kCommand_Parameters[] = "parameters";
const char kCommand_Progress[] = "progress";
const char kCommand_Results[] = "results";
const char kCommand_State[] = "state";

const char kCommand_Role[] = "minimalRole";
const char kCommand_Role_Manager[] = "manager";
const char kCommand_Role_Owner[] = "owner";
const char kCommand_Role_User[] = "user";
const char kCommand_Role_Viewer[] = "viewer";

const char kCommand_ErrorCode[] = "error.code";
const char kCommand_ErrorMessage[] = "error.message";

const char kCommand_Visibility[] = "visibility";
const char kCommand_Visibility_None[] = "none";
const char kCommand_Visibility_Local[] = "local";
const char kCommand_Visibility_Cloud[] = "cloud";
const char kCommand_Visibility_All[] = "all";

}  // namespace attributes
}  // namespace commands

}  // namespace buffet
