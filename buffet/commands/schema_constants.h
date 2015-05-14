// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BUFFET_COMMANDS_SCHEMA_CONSTANTS_H_
#define BUFFET_COMMANDS_SCHEMA_CONSTANTS_H_

namespace buffet {

namespace errors {
namespace commands {
// Error domain for command schema description.
extern const char kDomain[];

// Common command definition error codes.
extern const char kOutOfRange[];
extern const char kTypeMismatch[];
extern const char kPropTypeChanged[];
extern const char kUnknownType[];
extern const char kInvalidPropDef[];
extern const char kInvalidPropValue[];
extern const char kNoTypeInfo[];
extern const char kPropertyMissing[];
extern const char kUnknownProperty[];
extern const char kInvalidObjectSchema[];
extern const char kDuplicateCommandDef[];
extern const char kInvalidCommandName[];
extern const char kCommandFailed[];
extern const char kInvalidCommandVisibility[];
extern const char kInvalidMinimalRole[];
}  // namespace commands
}  // namespace errors

namespace commands {
namespace attributes {
// Command description JSON schema attributes.
extern const char kType[];
extern const char kDisplayName[];
extern const char kDefault[];
extern const char kItems[];

extern const char kNumeric_Min[];
extern const char kNumeric_Max[];

extern const char kString_MinLength[];
extern const char kString_MaxLength[];

extern const char kOneOf_Enum[];
extern const char kOneOf_Metadata[];

extern const char kObject_Properties[];
extern const char kObject_AdditionalProperties[];

extern const char kCommand_Id[];
extern const char kCommand_Name[];
extern const char kCommand_Parameters[];
extern const char kCommand_Progress[];
extern const char kCommand_Results[];
extern const char kCommand_State[];

extern const char kCommand_Role[];
extern const char kCommand_Role_Manager[];
extern const char kCommand_Role_Owner[];
extern const char kCommand_Role_User[];
extern const char kCommand_Role_Viewer[];

extern const char kCommand_ErrorCode[];
extern const char kCommand_ErrorMessage[];

extern const char kCommand_Visibility[];
extern const char kCommand_Visibility_None[];
extern const char kCommand_Visibility_Local[];
extern const char kCommand_Visibility_Cloud[];
extern const char kCommand_Visibility_All[];

}  // namespace attributes
}  // namespace commands

}  // namespace buffet

#endif  // BUFFET_COMMANDS_SCHEMA_CONSTANTS_H_
