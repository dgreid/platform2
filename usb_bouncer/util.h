// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef USB_BOUNCER_UTIL_H_
#define USB_BOUNCER_UTIL_H_

#include <unistd.h>

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include <base/bind.h>
#include <base/callback.h>
#include <base/files/file_path.h>
#include <base/files/scoped_file.h>
#include <base/time/time.h>
#include <brillo/files/safe_fd.h>
#include <google/protobuf/repeated_field.h>
#include <google/protobuf/timestamp.pb.h>
#include <metrics/metrics_library.h>

#include "usb_bouncer/usb_bouncer.pb.h"

namespace usb_bouncer {

using google::protobuf::Timestamp;
using EntryMap = google::protobuf::Map<google::protobuf::string, RuleEntry>;

constexpr char kUsbBouncerUser[] = "usb_bouncer";
constexpr char kUsbBouncerGroup[] = "usb_bouncer";

constexpr char kDefaultDbName[] = "devices.proto";
constexpr char kUserDbBaseDir[] = "/run/daemon-store/usb_bouncer";
constexpr char kUserDbParentDir[] = "device-db";

constexpr char kDBusPath[] = "/run/dbus/system_bus_socket";

constexpr uid_t kRootUid = 0;

constexpr int kDefaultWaitTimeoutInSeconds = 5;

enum class UMADeviceRecognized {
  kRecognized,
  kUnrecognized,
};

enum class UMAEventTiming {
  kLoggedOut = 0,
  kLoggedIn = 1,
  kLocked = 2,
  kMaxValue = kLocked,
};

std::string Hash(const std::string& content);
std::string Hash(const google::protobuf::RepeatedPtrField<std::string>& rules);

// Set USB devices to be authorized by default and authorize any devices that
// were left unauthorized. This is performed on unlock when USBGuard is
// disabled. If an error occurs, false is returned.
bool AuthorizeAll(const std::string& devpath = "/sys/devices");

// Invokes usbguard to get a rule corresponding to |devpath|. Note that
// |devpath| isn't actually a valid path until you prepend "/sys". This matches
// the behavior of udev. The return value is a allow-list rule from usbguard
// with the port specific fields removed.
std::string GetRuleFromDevPath(const std::string& devpath);

// Returns false for rules that should not be included in the allow-list at the
// lock screen. The basic idea is to exclude devices whose function cannot be
// performed if they are first plugged in at the lock screen. Some examples
// include printers, scanners, and USB storage devices.
bool IncludeRuleAtLockscreen(const std::string& rule);

// Returns false if rule is not a valid rule.
bool ValidateRule(const std::string& rule);

// Log device attach events to inform future changes in policy.
void UMALogDeviceAttached(MetricsLibrary* metrics,
                          const std::string& rule,
                          UMADeviceRecognized recognized,
                          UMAEventTiming timing);

// Returns the path where the user DB should be written if there is a user
// signed in and CrOS is unlocked. Otherwise, returns an empty path. In the
// multi-login case, the primary user's daemon-store is used.
base::FilePath GetUserDBDir();

// Returns true if a guest session is active.
bool IsGuestSession();

// Returns true if the lock screen is being shown. On a D-Bus failure true is
// returned because that is the safer failure state. This may result in some
// devices not being added to a user's allow-list, but that is safer than a
// malicious device being added to the allow-list while at the lock-screen.
bool IsLockscreenShown();

std::string StripLeadingPathSeparators(const std::string& path);

// Returns a set of all the rules present in |entries|. This serves as a
// filtering step prior to generating the rules configuration for
// usbguard-daemon so that there aren't duplicate rules. The rules are
// de-duplicated by string value ignoring any metadata like the time last used.
std::unordered_set<std::string> UniqueRules(const EntryMap& entries);

// Attempts to open the specified statefile at
// |base_path|/|parent|/|state_file_name| with the proper permissions. The
// parent directory and state file will be cleared if the ownership or
// permissions don't match. They will be created if they do not exist. If |lock|
// is true, this call blocks until an exclusive lock can be obtained for |path|.
// All runs of usb_bouncer are expected to be relatively fast (<250ms), so
// blocking should be ok.
brillo::SafeFD OpenStateFile(const base::FilePath& base_path,
                             const std::string& parent_dir,
                             const std::string& state_file_name,
                             bool lock);

// Returns true if |ready| returns true. If it returns true immediately, no
// further action is taken. Otherwise, the process is forked and the parent
// exits immediately. The child will wait until |ready| returns true or the
// |timeout| is reached. |message| is printed to the log as the reason for
// forking the process.
//
// This is used to avoid blocking udev while waiting on journald to finish
// setting up logging or D-Bus to be ready. |fork_func| is provided for
// testability (note that if fork_func returns non-zero, exit(0) is called).
bool ForkAndWaitIfNotReady(
    const base::RepeatingCallback<bool()> ready,
    const std::string message,
    const base::TimeDelta& timeout = base::TimeDelta::FromSeconds(5),
    base::Callback<pid_t()> fork_func = base::Bind(&fork));

void UpdateTimestamp(Timestamp* timestamp);
size_t RemoveEntriesOlderThan(base::TimeDelta cutoff, EntryMap* map);

}  // namespace usb_bouncer

#endif  // USB_BOUNCER_UTIL_H_
