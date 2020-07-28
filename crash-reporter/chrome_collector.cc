// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/chrome_collector.h"

#include <pcrecpp.h>
#include <stdint.h>

#include <map>
#include <string>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <brillo/data_encoding.h>
#include <brillo/process/process.h>
#include <brillo/syslog_logging.h>

#include "crash-reporter/util.h"

using base::FilePath;

namespace {

const char kDefaultMinidumpName[] = "upload_file_minidump";

// Filenames for logs attached to crash reports. Also used as metadata keys.
const char kChromeLogFilename[] = "chrome.txt";
const char kGpuStateFilename[] = "i915_error_state.log.xz";

// Filename for the pid of the browser process if it was aborted due to a
// browser hang. Written by session_manager.
constexpr char kAbortedBrowserPidPath[] = "/run/chrome/aborted_browser_pid";

// Extract a string delimited by the given character, from the given offset
// into a source string. Returns false if the string is zero-sized or no
// delimiter was found.
bool GetDelimitedString(const std::string& str,
                        char ch,
                        size_t offset,
                        std::string* substr) {
  size_t at = str.find_first_of(ch, offset);
  if (at == std::string::npos || at == offset)
    return false;
  *substr = str.substr(offset, at - offset);
  return true;
}

}  // namespace

ChromeCollector::ChromeCollector(CrashSendingMode crash_sending_mode)
    : CrashCollector("chrome",
                     kUseNormalCrashDirectorySelectionMethod,
                     crash_sending_mode),
      output_file_ptr_(stdout),
      max_upload_bytes_(util::kDefaultMaxUploadBytes) {}

ChromeCollector::~ChromeCollector() {}

bool ChromeCollector::HandleCrashWithDumpData(const std::string& data,
                                              pid_t pid,
                                              uid_t uid,
                                              const std::string& exe_name,
                                              const std::string& dump_dir) {
  // Perform basic input validation.
  CHECK(pid >= (pid_t)0) << "--pid= must be set";
  CHECK(uid >= (uid_t)0) << "--uid= must be set";
  CHECK(!exe_name.empty()) << "--exe= must be set";
  CHECK(dump_dir.empty() || util::IsTestImage())
      << "--chrome_dump_dir is only for tests";

  // anomaly_detector's CrashReporterParser looks for this message; don't change
  // it without updating the regex.
  LOG(WARNING) << "Received crash notification for " << exe_name << "[" << pid
               << "] user " << uid << " (called directly)";

  if (!is_feedback_allowed_function_()) {
    LOG(WARNING) << "consent not given - ignoring";
    return true;
  }

  if (exe_name.find('/') != std::string::npos) {
    LOG(ERROR) << "exe_name contains illegal characters: " << exe_name;
    return false;
  }

  FilePath dir;
  if (!dump_dir.empty()) {
    dir = FilePath(dump_dir);
  } else if (!GetCreatedCrashDirectoryByEuid(uid, &dir, nullptr)) {
    LOG(ERROR) << "Can't create crash directory for uid " << uid;
    return false;
  }

  std::string dump_basename = FormatDumpBasename(exe_name, time(nullptr), pid);
  FilePath meta_path = GetCrashPath(dir, dump_basename, "meta");
  FilePath minidump_path = GetCrashPath(dir, dump_basename, "dmp");

  if (!ParseCrashLog(data, dir, minidump_path, dump_basename)) {
    LOG(ERROR) << "Failed to parse Chrome's crash log";
    return false;
  }

  // Keyed by crash metadata key name.
  const std::map<std::string, base::FilePath> additional_logs =
      GetAdditionalLogs(dir, dump_basename, exe_name);
  for (const auto& it : additional_logs) {
    VLOG(1) << "Adding metadata: " << it.first << " -> " << it.second.value();
    // Call AddCrashMetaUploadFile() rather than AddCrashMetaData() here. The
    // former adds a prefix to the key name; without the prefix, only the key
    // "logs" appears to be displayed on the crash server.
    AddCrashMetaUploadFile(it.first, it.second.BaseName().value());
  }

  base::FilePath aborted_path(kAbortedBrowserPidPath);
  std::string pid_data;
  if (base::ReadFileToString(aborted_path, &pid_data)) {
    base::TrimWhitespaceASCII(pid_data, base::TRIM_TRAILING, &pid_data);
    if (pid_data == base::NumberToString(pid)) {
      AddCrashMetaUploadData("browser_hang", "true");
      base::DeleteFile(aborted_path, false);
    }
  }

  // We're done.
  FinishCrash(meta_path, exe_name, minidump_path.BaseName().value());

  // In production |output_file_ptr_| must be stdout because chrome expects to
  // read the magic string there.
  fprintf(output_file_ptr_, "%s", kSuccessMagic);
  fflush(output_file_ptr_);

  return true;
}

bool ChromeCollector::HandleCrash(const FilePath& file_path,
                                  pid_t pid,
                                  uid_t uid,
                                  const std::string& exe_name) {
  std::string data;
  if (!base::ReadFileToString(base::FilePath(file_path), &data)) {
    PLOG(ERROR) << "Can't read crash log: " << file_path.value();
    return false;
  }

  return HandleCrashWithDumpData(data, pid, uid, exe_name, "" /* dump_dir */);
}

bool ChromeCollector::HandleCrashThroughMemfd(int memfd,
                                              pid_t pid,
                                              uid_t uid,
                                              const std::string& exe_name,
                                              const std::string& dump_dir) {
  std::string data;
  if (!util::ReadMemfdToString(memfd, &data)) {
    PLOG(ERROR) << "Can't read crash log from memfd: " << memfd;
    return false;
  }

  return HandleCrashWithDumpData(data, pid, uid, exe_name, dump_dir);
}

bool ChromeCollector::ParseCrashLog(const std::string& data,
                                    const FilePath& dir,
                                    const FilePath& minidump,
                                    const std::string& basename) {
  size_t at = 0;
  while (at < data.size()) {
    // Look for a : followed by a decimal number, followed by another :
    // followed by N bytes of data.
    std::string name, size_string;
    if (!GetDelimitedString(data, ':', at, &name)) {
      LOG(ERROR) << "Can't find : after name @ offset " << at;
      break;
    }
    at += name.size() + 1;  // Skip the name & : delimiter.

    if (!GetDelimitedString(data, ':', at, &size_string)) {
      LOG(ERROR) << "Can't find : after size @ offset " << at;
      break;
    }
    at += size_string.size() + 1;  // Skip the size & : delimiter.

    size_t size;
    if (!base::StringToSizeT(size_string, &size)) {
      LOG(ERROR) << "String not convertible to integer: " << size_string;
      break;
    }

    // Data would run past the end, did we get a truncated file?
    if (at + size > data.size()) {
      LOG(ERROR) << "Overrun, expected " << size << " bytes of data, got "
                 << (data.size() - at);
      break;
    }

    if (name.find("filename") != std::string::npos) {
      // File.
      // Name will be in a semi-MIME format of
      // <descriptive name>"; filename="<name>"
      // Descriptive name will be upload_file_minidump for the dump.
      std::string desc, filename;
      pcrecpp::RE re("(.*)\" *; *filename=\"(.*)\"");
      if (!re.FullMatch(name.c_str(), &desc, &filename)) {
        LOG(ERROR) << "Filename was not in expected format: " << name;
        break;
      }

      if (desc.compare(kDefaultMinidumpName) == 0) {
        // The minidump.
        WriteNewFile(minidump, data.c_str() + at, size);
      } else {
        // Some other file.
        FilePath path =
            GetCrashPath(dir, basename + "-" + Sanitize(filename), "other");
        if (WriteNewFile(path, data.c_str() + at, size) >= 0) {
          AddCrashMetaUploadFile(desc, path.BaseName().value());
        }
      }
    } else {
      // Other attribute.
      std::string value_str;
      value_str.reserve(size);

      // Since metadata is one line/value the values must be escaped properly.
      for (size_t i = at; i < at + size; i++) {
        switch (data[i]) {
          case '"':
          case '\\':
            value_str.push_back('\\');
            value_str.push_back(data[i]);
            break;

          case '\r':
            value_str += "\\r";
            break;

          case '\n':
            value_str += "\\n";
            break;

          case '\t':
            value_str += "\\t";
            break;

          case '\0':
            value_str += "\\0";
            break;

          default:
            value_str.push_back(data[i]);
            break;
        }
      }
      AddCrashMetaUploadData(name, value_str);
    }

    at += size;
  }

  return at == data.size();
}

void ChromeCollector::AddLogIfNotTooBig(
    const char* log_map_key,
    const base::FilePath& complete_file_name,
    std::map<std::string, base::FilePath>* logs) {
  if (get_bytes_written() <= max_upload_bytes_) {
    (*logs)[log_map_key] = complete_file_name.BaseName();
  } else {
    // Logs were really big, don't upload them.
    LOG(WARNING) << "Skipping upload of " << complete_file_name.value()
                 << " because report size would exceed limit ("
                 << max_upload_bytes_ << "B)";
    // And free up resources to avoid leaving orphaned file around.
    if (!RemoveNewFile(complete_file_name)) {
      LOG(WARNING) << "Could not remove " << complete_file_name.value();
    }
  }
}

std::map<std::string, base::FilePath> ChromeCollector::GetAdditionalLogs(
    const FilePath& dir,
    const std::string& basename,
    const std::string& exe_name) {
  std::map<std::string, base::FilePath> logs;
  if (get_bytes_written() > max_upload_bytes_) {
    // Minidump is already too big, no point in processing logs or querying
    // debugd.
    LOG(WARNING) << "Skipping upload of supplemental logs because report size "
                 << "already exceeds limit (" << max_upload_bytes_ << "B)";
    return logs;
  }

  // Run the command specified by the config file to gather logs.
  const FilePath chrome_log_path =
      GetCrashPath(dir, basename, kChromeLogFilename).AddExtension("gz");
  if (GetLogContents(log_config_path_, exe_name, chrome_log_path)) {
    AddLogIfNotTooBig(kChromeLogFilename, chrome_log_path, &logs);
  }

  // For unit testing, debugd_proxy_ isn't initialized, so skip attempting to
  // get the GPU error state from debugd.
  SetUpDBus();
  if (debugd_proxy_) {
    const FilePath dri_error_state_path =
        GetCrashPath(dir, basename, kGpuStateFilename);
    if (GetDriErrorState(dri_error_state_path))
      AddLogIfNotTooBig(kGpuStateFilename, dri_error_state_path, &logs);
  }

  return logs;
}

bool ChromeCollector::GetDriErrorState(const FilePath& error_state_path) {
  brillo::ErrorPtr error;
  std::string error_state_str;
  // Chrome has a 12 second timeout for crash_reporter to execute when it
  // invokes it, so use a 5 second timeout here on our D-Bus call.
  constexpr int kDebugdGetLogTimeoutMsec = 5000;
  debugd_proxy_->GetLog("i915_error_state", &error_state_str, &error,
                        kDebugdGetLogTimeoutMsec);

  if (error) {
    LOG(ERROR) << "Error calling D-Bus proxy call to interface "
               << "'" << debugd_proxy_->GetObjectPath().value()
               << "':" << error->GetMessage();
    return false;
  }

  if (error_state_str == "<empty>")
    return false;

  const char kBase64Header[] = "<base64>: ";
  const size_t kBase64HeaderLength = sizeof(kBase64Header) - 1;
  if (error_state_str.compare(0, kBase64HeaderLength, kBase64Header)) {
    LOG(ERROR) << "i915_error_state is missing base64 header";
    return false;
  }

  std::string decoded_error_state;

  if (!brillo::data_encoding::Base64Decode(
          error_state_str.c_str() + kBase64HeaderLength,
          &decoded_error_state)) {
    LOG(ERROR) << "Could not decode i915_error_state";
    return false;
  }

  // We must use WriteNewFile instead of base::WriteFile as we
  // do not want to write with root access to a symlink that an attacker
  // might have created.
  int written = WriteNewFile(error_state_path, decoded_error_state.c_str(),
                             decoded_error_state.length());
  if (written < 0 ||
      static_cast<size_t>(written) != decoded_error_state.length()) {
    PLOG(ERROR) << "Could not write file " << error_state_path.value()
                << " Written: " << written
                << " Len: " << decoded_error_state.length();
    base::DeleteFile(error_state_path, false);
    return false;
  }

  return true;
}

// See chrome's src/components/crash/content/app/breakpad_linux.cc.
// static
const char ChromeCollector::kSuccessMagic[] = "_sys_cr_finished";
