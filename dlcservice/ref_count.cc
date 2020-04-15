// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <set>
#include <string>

#include <base/files/file_path.h>
#include <brillo/errors/error.h>
#include <chromeos/dbus/service_constants.h>

#include "dlcservice/error.h"
#include "dlcservice/ref_count.h"
#include "dlcservice/system_state.h"
#include "dlcservice/utils.h"

using std::set;
using std::string;
using std::unique_ptr;

namespace dlcservice {

const char kRefCountFileName[] = "ref_count.bin";
const char kSessionStarted[] = "started";
const char kUsedByUser[] = "user";
const char kUsedBySystem[] = "system";

const int kDefaultExpirationDelayDays = 5;
const char kSystemUsername[] = "system";

// static
std::unique_ptr<RefCountInterface> RefCountInterface::Create(
    const string& used_by, const base::FilePath& prefs_path) {
  if (used_by == "user") {
    return std::make_unique<UserRefCount>(prefs_path);
  } else if (used_by == "system") {
    return std::make_unique<SystemRefCount>(prefs_path);
  } else {
    NOTREACHED() << "Invalid 'used_by' attribute in manifest: " << used_by;
  }
  return nullptr;
}

RefCountBase::RefCountBase(const base::FilePath& prefs_path) {
  // Load the ref count proto only if it exists.
  ref_count_path_ = prefs_path.Append(kRefCountFileName);
  if (base::PathExists(ref_count_path_)) {
    string ref_count_str;
    if (base::ReadFileToString(ref_count_path_, &ref_count_str))
      ref_count_info_.ParseFromString(ref_count_str);
  }
}

bool RefCountBase::InstalledDlc() {
  string username = GetCurrentUserName();
  if (username.empty()) {
    // Probably no user has logged in. So discard.
    return true;
  }

  // If we already have the user, ignore.
  if (std::any_of(ref_count_info_.users().begin(),
                  ref_count_info_.users().end(),
                  [&username](const RefCountInfo::User& user) {
                    return user.sanitized_username() == username;
                  })) {
    return true;
  }

  // Add the current user to the list of users for this DLC.
  ref_count_info_.add_users()->set_sanitized_username(username);
  return Persist();
}

bool RefCountBase::UninstalledDlc() {
  string username = GetCurrentUserName();
  if (username.empty()) {
    // Probably no user has logged in. So discard.
    return true;
  }

  // If we don't have the user, ignore.
  auto user_it = std::find_if(ref_count_info_.users().begin(),
                              ref_count_info_.users().end(),
                              [&username](const RefCountInfo::User& user) {
                                return user.sanitized_username() == username;
                              });
  if (user_it == ref_count_info_.users().end())
    return true;

  // Remove the user from the list of users currently using this DLC.
  ref_count_info_.mutable_users()->erase(user_it);
  return Persist();
}

bool RefCountBase::ShouldPurgeDlc() const {
  // If someone is using it, it should not be removed.
  if (ref_count_info_.users_size() != 0) {
    return false;
  }

  // If the last access time has not been set, then we don't know the timeline
  // and this DLC should not be removed.
  int64_t last_access_time_us = ref_count_info_.last_access_time_us();
  if (last_access_time_us == 0) {
    return false;
  }

  base::Time last_accessed = base::Time::FromDeltaSinceWindowsEpoch(
      base::TimeDelta::FromMicroseconds(last_access_time_us));
  base::TimeDelta delta_time = base::Time::Now() - last_accessed;
  return delta_time > GetExpirationDelay();
}

bool RefCountBase::Persist() {
  ref_count_info_.set_last_access_time_us(
      base::Time::Now().ToDeltaSinceWindowsEpoch().InMicroseconds());

  string ref_count_str;
  if (!ref_count_info_.SerializeToString(&ref_count_str)) {
    LOG(ERROR) << "Failed to serialize user based ref count proto.";
    return false;
  }
  if (!WriteToFile(ref_count_path_, ref_count_str)) {
    PLOG(ERROR) << "Failed to write user based ref count proto to: "
                << ref_count_path_;
    return false;
  }
  return true;
}

// UserRefCount implementations.
set<string> UserRefCount::user_names_;
unique_ptr<string> UserRefCount::primary_session_username_;

// static
void UserRefCount::SessionChanged(const string& state) {
  if (state == kSessionStarted) {
    user_names_ = ScanDirectory(SystemState::Get()->users_dir());

    string username, sanitized_username;
    brillo::ErrorPtr err;
    if (!SystemState::Get()->session_manager()->RetrievePrimarySession(
            &username, &sanitized_username, &err)) {
      LOG(ERROR) << "Failed to get the primary session's username with error: "
                 << Error::ToString(err);
      primary_session_username_.release();
      return;
    }
    primary_session_username_ = std::make_unique<string>(sanitized_username);
  }
}

}  // namespace dlcservice
