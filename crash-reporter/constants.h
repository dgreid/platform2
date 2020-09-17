// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRASH_REPORTER_CONSTANTS_H_
#define CRASH_REPORTER_CONSTANTS_H_

namespace constants {

constexpr char kCrashName[] = "crash";
constexpr char kCrashGroupName[] = "crash-access";

#if !USE_KVM_GUEST
constexpr char kCrashUserGroupName[] = "crash-user-access";
#endif

constexpr char kUploadVarPrefix[] = "upload_var_";
constexpr char kUploadTextPrefix[] = "upload_text_";
constexpr char kUploadFilePrefix[] = "upload_file_";

constexpr char kJavaScriptStackExtension[] = "js_stack";
constexpr char kJavaScriptStackExtensionWithDot[] = ".js_stack";
// This *must match* the crash::FileStorage::kJsStacktraceFileName constant
// in the google3 internal crash processing code.
constexpr char kKindForJavaScriptError[] = "JavascriptError";

constexpr char kMinidumpExtension[] = "dmp";
constexpr char kMinidumpExtensionWithDot[] = ".dmp";
// This *must match* the ending of the crash::FileStorage::kDumpFileName
// in the google3 internal crash processing code.
constexpr char kKindForMinidump[] = "minidump";

}  // namespace constants

#endif  // CRASH_REPORTER_CONSTANTS_H_
