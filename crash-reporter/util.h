// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRASH_REPORTER_UTIL_H_
#define CRASH_REPORTER_UTIL_H_

#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/time/clock.h>
#include <base/time/time.h>
#include <brillo/process/process.h>
#include <brillo/streams/stream.h>
#include <metrics/metrics_library.h>
#include <session_manager/dbus-proxies.h>

namespace util {

// From //net/crash/collector/collector.h
extern const int kDefaultMaxUploadBytes;

// Returns true if integration tests are currently running.
bool IsCrashTestInProgress();

// Returns true if uploading of device coredumps is allowed.
bool IsDeviceCoredumpUploadAllowed();

// Returns true if running on a developer image.
bool IsDeveloperImage();

// Returns true if running on a test image.
bool IsTestImage();

// Returns true if running on an official image.
bool IsOfficialImage();

// Returns true if we are mocking metrics consent as granted.
bool HasMockConsent();

// Determines whether feedback is allowed, based on:
// * The presence/absence of mock consent
// * Whether this is a developer image
// * Whether the metrics library indicates consent
// Does not take ownership of |metrics_lib|
bool IsFeedbackAllowed(MetricsLibraryInterface* metrics_lib);

// Returns true if we should skip crash collection (based on the filter-in
// file).
// Specifically, if the file exists, crash_reporter will exit early unless its
// contents are a substring of the command-line parameters.
// Alternatively, if the file contains the string "none", then crash_reporter
// will always exit early.
bool SkipCrashCollection(int argc, char* argv[]);

// Change group ownership of "file" to "group", and grant g+rw (optionally x).
bool SetGroupAndPermissions(const base::FilePath& file,
                            const char* group,
                            bool execute);

// Returns the timestamp for the OS version we are currently running. Returns
// a null (zero-valued) base::Time if it is unable to calculate it for some
// reason.
base::Time GetOsTimestamp();

// Returns true if this version is old enough that we do not want to upload the
// crash reports anymore. This just checks if |timestamp| is more than 180
// days old. If |timestamp| is null (zero-valued) then this will return false.
bool IsOsTimestampTooOldForUploads(base::Time timestamp, base::Clock* clock);

// Gets a string describing the hardware class of the device. Returns
// "undefined" if this cannot be determined.
std::string GetHardwareClass();

// Returns the boot mode which will either be "dev", "missing-crossystem" (if it
// cannot be determined) or the empty string.
std::string GetBootModeString();

// Tries to find |key| in a key-value file named |base_name| in |directories| in
// the specified order, and writes the value to |value|. This function returns
// as soon as the key is found (i.e. if the key is found in the first directory,
// the remaining directories won't be checked). Returns true on success.
bool GetCachedKeyValue(const base::FilePath& base_name,
                       const std::string& key,
                       const std::vector<base::FilePath>& directories,
                       std::string* value);

// Similar to GetCachedKeyValue(), but this version checks the predefined
// default directories.
bool GetCachedKeyValueDefault(const base::FilePath& base_name,
                              const std::string& key,
                              std::string* value);

// Gets the user crash directories via D-Bus using |session_manager_proxy|.
// Returns true on success.
bool GetUserCrashDirectories(
    org::chromium::SessionManagerInterfaceProxyInterface* session_manager_proxy,
    std::vector<base::FilePath>* directories);

bool GetDaemonStoreCrashDirectories(
    org::chromium::SessionManagerInterfaceProxyInterface* session_manager_proxy,
    std::vector<base::FilePath>* directories);

// Gzip's the |data| passed in and returns the compressed data. Returns an empty
// vector on failure.
std::vector<unsigned char> GzipStream(brillo::StreamPtr data);

// Runs |process| and redirects |fd| to |output|. Returns the exit code, or -1
// if the process failed to start.
int RunAndCaptureOutput(brillo::ProcessImpl* process,
                        int fd,
                        std::string* output);

// Breaks up |error| using std::getline and then does a LOG(ERROR) of each
// individual line.
void LogMultilineError(const std::string& error);

// Read the memfd file contents. Return false on failure.
bool ReadMemfdToString(int mem_fd, std::string* contents);

// Return the weight for SELinux failures. We'll only collect
// 1.0/GetSelinuxWeight() of the failures.
int GetSelinuxWeight();

// Return the weight for service failures. We'll only collect
// 1.0/GetServiceFailureWeight() of the failures.
int GetServiceFailureWeight();

// Read the content binding to fd to stream.
bool ReadFdToStream(unsigned int fd, std::stringstream* stream);

#if USE_DIRENCRYPTION
// Joins the session key if the kernel supports ext4 directory encryption.
void JoinSessionKeyring();
#endif  // USE_DIRENCRYPTION

}  // namespace util

#endif  // CRASH_REPORTER_UTIL_H_
