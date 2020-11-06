// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/arc_util.h"

namespace arc_util {

namespace {

bool HasExceptionInfo(const std::string& type) {
  static const std::unordered_set<std::string> kTypes = {
      "data_app_crash", "system_app_crash", "system_app_wtf",
      "system_server_crash", "system_server_wtf"};
  return kTypes.count(type);
}

base::TimeTicks ToSeconds(const base::TimeTicks& time) {
  return base::TimeTicks::FromInternalValue(
      base::TimeDelta::FromSeconds(
          base::TimeDelta::FromInternalValue(time.ToInternalValue())
              .InSeconds())
          .ToInternalValue());
}

}  // namespace

using CrashLogHeaderMap = std::unordered_map<std::string, std::string>;

const char kArcProduct[] = "ChromeOS_ARC";

const char kAbiMigrationField[] = "abi_migration_status";
const char kAndroidVersionField[] = "android_version";
const char kArcVersionField[] = "arc_version";
const char kBoardField[] = "board";
const char kChromeOsVersionField[] = "chrome_os_version";
const char kCpuAbiField[] = "cpu_abi";
const char kCrashTypeField[] = "crash_type";
const char kDeviceField[] = "device";
const char kProcessField[] = "process";
const char kProductField[] = "prod";
const char kUptimeField[] = "uptime";

const char kExceptionInfoField[] = "exception_info";
const char kSignatureField[] = "sig";

const char kSilentKey[] = "silent";

const char kProcessKey[] = "Process";
const char kSubjectKey[] = "Subject";

const std::vector<std::pair<const char*, const char*>> kHeaderToFieldMapping = {
    {"Crash-Tag", "crash_tag"},
    {"NDK-Execution", "ndk_execution"},
    {"Package", "package"},
    {"Target-SDK", "target_sdk"},
    {"Abi-Migration-Status", "abi_migration_status"},
};

base::Optional<std::string> GetVersionFromFingerprint(
    const std::string& fingerprint) {
  // fingerprint has the following format:
  //   $(PRODUCT_BRAND)/$(TARGET_PRODUCT)/$(TARGET_DEVICE):$(PLATFORM_VERSION)/
  //     ..$(BUILD_ID)/$(BF_BUILD_NUMBER):$(TARGET_BUILD_VARIANT)/
  //     ..$(BUILD_VERSION_TAGS)
  // eg:
  //   google/caroline/caroline_cheets:7.1.1/R65-10317.0.9999/
  //     ..4548207:user/release-keys
  // we want to get the $(PLATFORM_VERSION). eg: 7.1.1

  std::string android_version;
  // Assuming the fingerprint format won't change. Everything between ':' and
  // '/R' is the version.
  auto begin = fingerprint.find(':');
  if (begin == std::string::npos)
    return base::nullopt;

  // Make begin point to the start of the "version".
  begin++;

  // Version must have at least one digit.
  const auto end = fingerprint.find("/R", begin + 1);
  if (end == std::string::npos)
    return base::nullopt;

  return fingerprint.substr(begin, end - begin);
}

bool ParseCrashLog(const std::string& type,
                   std::stringstream* stream,
                   std::unordered_map<std::string, std::string>* map,
                   std::string* exception_info,
                   std::string* log) {
  std::string line;

  // The last header is followed by an empty line.
  while (std::getline(*stream, line) && !line.empty()) {
    const auto end = line.find(':');

    if (end != std::string::npos) {
      const auto begin = line.find_first_not_of(' ', end + 1);

      if (begin != std::string::npos) {
        // TODO(domlaskowski): Use multimap to allow multiple "Package" headers.
        if (!map->emplace(line.substr(0, end), line.substr(begin)).second)
          LOG(WARNING) << "Duplicate header: " << line;
        continue;
      }
    }

    // Ignore malformed headers. The report is still created, but the associated
    // metadata fields are set to "unknown".
    LOG(WARNING) << "Header has unexpected format: " << line;
  }

  if (stream->fail())
    return false;

  if (HasExceptionInfo(type)) {
    std::ostringstream out;
    out << stream->rdbuf();
    *exception_info = out.str();
  }
  *log = stream->str();

  return true;
}

const char* GetSubjectTag(const std::string& type) {
  static const CrashLogHeaderMap kTags = {
      {"data_app_native_crash", "native app crash"},
      {"system_app_anr", "ANR"},
      {"data_app_anr", "app ANR"},
      {"system_server_watchdog", "system server watchdog"}};

  const auto it = kTags.find(type);
  return it == kTags.cend() ? nullptr : it->second.c_str();
}

bool IsSilentReport(const std::string& type) {
  return type == "system_app_wtf" || type == "system_server_wtf";
}

std::string GetCrashLogHeader(const CrashLogHeaderMap& map, const char* key) {
  const auto it = map.find(key);
  return it == map.end() ? "unknown" : it->second;
}

pid_t CreateRandomPID() {
  const auto now = base::TimeTicks::Now();
  return (now - ToSeconds(now)).InMicroseconds();
}

}  // namespace arc_util
