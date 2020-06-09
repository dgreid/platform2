// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <iterator>
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
  last_access_time_us_ = 0;

  // Load the ref count proto only if it exists.
  ref_count_path_ = prefs_path.Append(kRefCountFileName);
  if (base::PathExists(ref_count_path_)) {
    RefCountInfo info;
    if (ReadRefCountInfo(ref_count_path_, &info)) {
      for (const auto& user : info.users()) {
        users_.insert(user.sanitized_username());
      }
      last_access_time_us_ = info.last_access_time_us();
    }
  }
}

// static
bool RefCountBase::ReadRefCountInfo(const base::FilePath& path,
                                    RefCountInfo* info) {
  DCHECK(info);
  string info_str;
  if (!base::ReadFileToString(path, &info_str)) {
    PLOG(ERROR) << "Failed to read the ref count proto file: " << path.value();
    return false;
  }
  if (!info->ParseFromString(info_str)) {
    LOG(ERROR) << "Failed to parse the ref count proto file: " << path.value();
    return false;
  }
  return true;
}

bool RefCountBase::InstalledDlc() {
  string username = GetCurrentUserName();
  if (username.empty()) {
    // Probably no user has logged in. So discard.
    return true;
  }

  // If we already have the user, ignore.
  if (users_.find(username) != users_.end())
    return true;

  // Add the current user to the list of users for this DLC.
  users_.insert(username);
  return Persist();
}

bool RefCountBase::UninstalledDlc() {
  string username = GetCurrentUserName();
  if (username.empty()) {
    // Probably no user has logged in. So discard.
    return true;
  }

  auto user_it = users_.find(username);
  // If we don't have this user, ignore.
  if (user_it == users_.end())
    return true;

  // Remove the user from the list of users currently using this DLC.
  users_.erase(user_it);
  return Persist();
}

bool RefCountBase::ShouldPurgeDlc() const {
  // If someone is using it, it should not be removed.
  if (users_.size() != 0) {
    return false;
  }

  // If the last access time has not been set, then we don't know the timeline
  // and this DLC should not be removed.
  if (last_access_time_us_ == 0) {
    return false;
  }

  base::Time last_accessed = base::Time::FromDeltaSinceWindowsEpoch(
      base::TimeDelta::FromMicroseconds(last_access_time_us_));
  base::TimeDelta delta_time =
      SystemState::Get()->clock()->Now() - last_accessed;
  return delta_time > GetExpirationDelay();
}

bool RefCountBase::Persist() {
  last_access_time_us_ = SystemState::Get()
                             ->clock()
                             ->Now()
                             .ToDeltaSinceWindowsEpoch()
                             .InMicroseconds();

  RefCountInfo info;
  info.set_last_access_time_us(last_access_time_us_);
  for (const auto& username : users_) {
    info.add_users()->set_sanitized_username(username);
  }

  string info_str;
  if (!info.SerializeToString(&info_str)) {
    LOG(ERROR) << "Failed to serialize user based ref count proto.";
    return false;
  }
  if (!WriteToFile(ref_count_path_, info_str)) {
    PLOG(ERROR) << "Failed to write user based ref count proto to: "
                << ref_count_path_;
    return false;
  }
  return true;
}

// UserRefCount implementations.
set<string> UserRefCount::device_users_;
unique_ptr<string> UserRefCount::primary_session_username_;

// static
void UserRefCount::SessionChanged(const string& state) {
  if (state == kSessionStarted) {
    device_users_ = ScanDirectory(SystemState::Get()->users_dir());

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

UserRefCount::UserRefCount(const base::FilePath& prefs_path)
    : RefCountBase(prefs_path) {
  // We are only interested in users that exist on the system. Any other user
  // that don't exist in the system, but is included in the ref count should be
  // ignored. We don't necessarily need to delete these dangling users from the
  // proto file itself because one, that user might come back, and two it
  // doesn't really matter to the logic of ref counts because when we load, we
  // only care about the users we loaded and approved. On the next install or
  // uninstall the correct users will be persisted.
  set<string> intersection;
  std::set_intersection(users_.begin(), users_.end(), device_users_.begin(),
                        device_users_.end(),
                        std::inserter(intersection, intersection.end()));
  users_.swap(intersection);
}

}  // namespace dlcservice
