// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/disk_cleanup.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <base/logging.h>
#include <base/optional.h>
#include <base/time/time.h>
#include <base/timer/elapsed_timer.h>

#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/disk_cleanup_routines.h"
#include "cryptohome/filesystem_layout.h"
#include "cryptohome/homedirs.h"
#include "cryptohome/platform.h"
#include "cryptohome/user_oldest_activity_timestamp_cache.h"

namespace cryptohome {

DiskCleanup::DiskCleanup(Platform* platform,
                         HomeDirs* homedirs,
                         UserOldestActivityTimestampCache* timestamp_cache)
    : platform_(platform),
      homedirs_(homedirs),
      timestamp_cache_(timestamp_cache),
      routines_(std::make_unique<DiskCleanupRoutines>(homedirs_, platform_)) {}

base::Optional<int64_t> DiskCleanup::AmountOfFreeDiskSpace() const {
  int64_t free_space = platform_->AmountOfFreeDiskSpace(ShadowRoot());

  if (free_space < 0) {
    return base::nullopt;
  } else {
    return free_space;
  }
}

DiskCleanup::FreeSpaceState DiskCleanup::GetFreeDiskSpaceState() const {
  return GetFreeDiskSpaceState(AmountOfFreeDiskSpace());
}

DiskCleanup::FreeSpaceState DiskCleanup::GetFreeDiskSpaceState(
    base::Optional<int64_t> free_disk_space) const {
  if (!free_disk_space) {
    return DiskCleanup::FreeSpaceState::kError;
  }

  int64_t value = free_disk_space.value();
  if (value >= target_free_space_) {
    return DiskCleanup::FreeSpaceState::kAboveTarget;
  } else if (value >= normal_cleanup_threshold_) {
    return DiskCleanup::FreeSpaceState::kAboveThreshold;
  } else if (value >= aggressive_cleanup_threshold_) {
    return DiskCleanup::FreeSpaceState::kNeedNormalCleanup;
  } else {
    return DiskCleanup::FreeSpaceState::kNeedAggressiveCleanup;
  }
}

bool DiskCleanup::HasTargetFreeSpace() const {
  return GetFreeDiskSpaceState() == DiskCleanup::FreeSpaceState::kAboveTarget;
}

bool DiskCleanup::IsFreeableDiskSpaceAvailable() {
  if (!homedirs_->enterprise_owned())
    return false;

  const auto homedirs = homedirs_->GetHomeDirs();

  int unmounted_cryptohomes =
      std::count_if(homedirs.begin(), homedirs.end(),
                    [](auto& dir) { return !dir.is_mounted; });

  return unmounted_cryptohomes > 0;
}

void DiskCleanup::FreeDiskSpace() {
  auto free_space = AmountOfFreeDiskSpace();

  switch (GetFreeDiskSpaceState(free_space)) {
    case DiskCleanup::FreeSpaceState::kAboveTarget:
    case DiskCleanup::FreeSpaceState::kAboveThreshold:
      // Already have enough space. No need to clean up.
      return;

    case DiskCleanup::FreeSpaceState::kNeedNormalCleanup:
    case DiskCleanup::FreeSpaceState::kNeedAggressiveCleanup:
      // trigger cleanup
      break;

    case DiskCleanup::FreeSpaceState::kError:
      LOG(ERROR) << "Failed to get the amount of free disk space";
      return;
    default:
      LOG(ERROR) << "Unhandled free disk state";
      return;
  }

  auto now = platform_->GetCurrentTime();

  if (last_free_disk_space_) {
    auto diff = now - *last_free_disk_space_;

    ReportTimeBetweenFreeDiskSpace(diff.InSeconds());
  }

  last_free_disk_space_ = now;

  base::ElapsedTimer total_timer;

  FreeDiskSpaceInternal();

  int cleanup_time = total_timer.Elapsed().InMilliseconds();
  ReportFreeDiskSpaceTotalTime(cleanup_time);
  VLOG(1) << "Disk cleanup took " << cleanup_time << "ms.";

  auto after_cleanup = AmountOfFreeDiskSpace();
  if (!after_cleanup) {
    LOG(ERROR) << "Failed to get the amount of free disk space";
    return;
  }

  ReportFreeDiskSpaceTotalFreedInMb(
      MAX(0, after_cleanup.value() - free_space.value()) / 1024 / 1024);

  LOG(INFO) << "Disk cleanup complete.";
}

void DiskCleanup::set_routines_for_testing(DiskCleanupRoutines* routines) {
  routines_.reset(routines);
}

void DiskCleanup::FreeDiskSpaceInternal() {
  // If ephemeral users are enabled, remove all cryptohomes except those
  // currently mounted or belonging to the owner.
  // |AreEphemeralUsers| will reload the policy to guarantee freshness.
  if (homedirs_->AreEphemeralUsersEnabled()) {
    homedirs_->RemoveNonOwnerCryptohomes();
    ReportDiskCleanupProgress(
        DiskCleanupProgress::kEphemeralUserProfilesCleaned);
    return;
  }

  auto homedirs = homedirs_->GetHomeDirs();

  // Initialize user timestamp cache if it has not been yet. This reads the
  // last-activity time from each homedir's SerializedVaultKeyset.  This value
  // is only updated in the value keyset on unmount and every 24 hrs, so a
  // currently logged in user probably doesn't have an up to date value. This
  // is okay, since we don't delete currently logged in homedirs anyway.  (See
  // Mount::UpdateCurrentUserActivityTimestamp()).
  if (!timestamp_cache_->initialized()) {
    timestamp_cache_->Initialize();
    for (const auto& dir : homedirs) {
      homedirs_->AddUserTimestampToCache(dir.obfuscated);
    }
  }

  auto unmounted_homedirs = homedirs;
  FilterMountedHomedirs(&unmounted_homedirs);

  std::sort(
      unmounted_homedirs.begin(), unmounted_homedirs.end(),
      [&](const HomeDirs::HomeDir& a, const HomeDirs::HomeDir& b) {
        return timestamp_cache_->GetLastUserActivityTimestamp(a.obfuscated) >
               timestamp_cache_->GetLastUserActivityTimestamp(b.obfuscated);
      });

  auto normal_cleanup_homedirs = unmounted_homedirs;

  if (last_normal_disk_cleanup_complete_) {
    base::Time cutoff = last_normal_disk_cleanup_complete_.value();
    FilterHomedirsProcessedBeforeCutoff(cutoff, &normal_cleanup_homedirs);
  }

  // Clean Cache directories for every unmounted user that has logged out after
  // the last normal cleanup happened.
  for (auto dir = normal_cleanup_homedirs.rbegin();
       dir != normal_cleanup_homedirs.rend(); dir++) {
    routines_->DeleteUserCache(dir->obfuscated);

    if (HasTargetFreeSpace()) {
      ReportDiskCleanupProgress(
          DiskCleanupProgress::kBrowserCacheCleanedAboveTarget);
      return;
    }
  }

  auto freeDiskSpace = AmountOfFreeDiskSpace();
  if (!freeDiskSpace) {
    LOG(ERROR) << "Failed to get the amount of free space";
    return;
  }

  bool earlyStop = false;

  // Clean GCache directories for every unmounted user that has logged out after
  // after the last normal cleanup happened.
  for (auto dir = normal_cleanup_homedirs.rbegin();
       dir != normal_cleanup_homedirs.rend(); dir++) {
    routines_->DeleteUserGCache(dir->obfuscated);

    if (HasTargetFreeSpace()) {
      earlyStop = true;
      break;
    }
  }

  if (!earlyStop)
    last_normal_disk_cleanup_complete_ = platform_->GetCurrentTime();

  const auto old_free_disk_space = freeDiskSpace;
  freeDiskSpace = AmountOfFreeDiskSpace();
  if (!freeDiskSpace) {
    LOG(ERROR) << "Failed to get the amount of free space";
    return;
  }

  const int64_t freed_gcache_space =
      freeDiskSpace.value() - old_free_disk_space.value();
  // Report only if something was deleted.
  if (freed_gcache_space > 0) {
    ReportFreedGCacheDiskSpaceInMb(freed_gcache_space / 1024 / 1024);
  }

  switch (GetFreeDiskSpaceState(freeDiskSpace)) {
    case DiskCleanup::FreeSpaceState::kAboveTarget:
      ReportDiskCleanupProgress(
          DiskCleanupProgress::kGoogleDriveCacheCleanedAboveTarget);
      return;
    case DiskCleanup::FreeSpaceState::kAboveThreshold:
    case DiskCleanup::FreeSpaceState::kNeedNormalCleanup:
      ReportDiskCleanupProgress(
          DiskCleanupProgress::kGoogleDriveCacheCleanedAboveMinimum);
      return;
    case DiskCleanup::FreeSpaceState::kNeedAggressiveCleanup:
      // continue cleanup
      break;
    case DiskCleanup::FreeSpaceState::kError:
      LOG(ERROR) << "Failed to get the amount of free space";
      return;
    default:
      LOG(ERROR) << "Unhandled free disk state";
      return;
  }

  auto aggressive_cleanup_homedirs = unmounted_homedirs;

  if (last_aggressive_disk_cleanup_complete_) {
    base::Time cutoff = last_aggressive_disk_cleanup_complete_.value();
    FilterHomedirsProcessedBeforeCutoff(cutoff, &aggressive_cleanup_homedirs);
  }

  // Clean Android cache directories for every unmounted user that has logged
  // out after after the last normal cleanup happened.
  for (auto dir = aggressive_cleanup_homedirs.rbegin();
       dir != aggressive_cleanup_homedirs.rend(); dir++) {
    routines_->DeleteUserAndroidCache(dir->obfuscated);

    if (HasTargetFreeSpace()) {
      earlyStop = true;
      break;
    }
  }

  if (!earlyStop)
    last_aggressive_disk_cleanup_complete_ = platform_->GetCurrentTime();

  switch (GetFreeDiskSpaceState()) {
    case DiskCleanup::FreeSpaceState::kAboveTarget:
      ReportDiskCleanupProgress(
          DiskCleanupProgress::kAndroidCacheCleanedAboveTarget);
      return;
    case DiskCleanup::FreeSpaceState::kAboveThreshold:
    case DiskCleanup::FreeSpaceState::kNeedNormalCleanup:
      ReportDiskCleanupProgress(
          DiskCleanupProgress::kAndroidCacheCleanedAboveMinimum);
      return;
    case DiskCleanup::FreeSpaceState::kNeedAggressiveCleanup:
      // continue cleanup
      break;
    case DiskCleanup::FreeSpaceState::kError:
      LOG(ERROR) << "Failed to get the amount of free space";
      return;
    default:
      LOG(ERROR) << "Unhandled free disk state";
      return;
  }

  // Delete old users, the oldest first. Count how many are deleted.
  // Don't delete anyone if we don't know who the owner is.
  // For consumer devices, don't delete the device owner. Enterprise-enrolled
  // devices have no owner, so don't delete the most-recent user.
  int deleted_users_count = 0;
  std::string owner;
  if (!homedirs_->enterprise_owned() && !homedirs_->GetOwner(&owner))
    return;

  int mounted_cryptohomes_count =
      std::count_if(homedirs.begin(), homedirs.end(),
                    [](auto& dir) { return dir.is_mounted; });

  for (auto dir = unmounted_homedirs.rbegin(); dir != unmounted_homedirs.rend();
       dir++) {
    if (homedirs_->enterprise_owned()) {
      // Leave the most-recent user on the device intact.
      // The most-recent user is the first in unmounted_homedirs.
      if (dir == unmounted_homedirs.rend() - 1 &&
          mounted_cryptohomes_count == 0) {
        LOG(INFO) << "Skipped deletion of the most recent device user.";
        continue;
      }
    } else if (dir->obfuscated == owner) {
      // We never delete the device owner.
      LOG(INFO) << "Skipped deletion of the device owner.";
      continue;
    }

    LOG(INFO) << "Freeing disk space by deleting user " << dir->obfuscated;
    routines_->DeleteUserProfile(dir->obfuscated);
    timestamp_cache_->RemoveUser(dir->obfuscated);
    ++deleted_users_count;

    if (HasTargetFreeSpace())
      break;
  }

  if (deleted_users_count > 0) {
    ReportDeletedUserProfiles(deleted_users_count);
  }

  // We had a chance to delete a user only if any unmounted homes existed.
  if (unmounted_homedirs.size() > 0) {
    ReportDiskCleanupProgress(
        HasTargetFreeSpace()
            ? DiskCleanupProgress::kWholeUserProfilesCleanedAboveTarget
            : DiskCleanupProgress::kWholeUserProfilesCleaned);
  } else {
    ReportDiskCleanupProgress(DiskCleanupProgress::kNoUnmountedCryptohomes);
  }
}

void DiskCleanup::FilterMountedHomedirs(
    std::vector<HomeDirs::HomeDir>* homedirs) {
  homedirs->erase(std::remove_if(homedirs->begin(), homedirs->end(),
                                 [](const HomeDirs::HomeDir& dir) {
                                   return dir.is_mounted;
                                 }),
                  homedirs->end());
}

void DiskCleanup::FilterHomedirsProcessedBeforeCutoff(
    base::Time cutoff, std::vector<HomeDirs::HomeDir>* homedirs) {
  homedirs->erase(
      std::remove_if(homedirs->begin(), homedirs->end(),
                     [&](const HomeDirs::HomeDir& dir) {
                       return timestamp_cache_->GetLastUserActivityTimestamp(
                                  dir.obfuscated) < cutoff;
                     }),
      homedirs->end());
}

}  // namespace cryptohome
