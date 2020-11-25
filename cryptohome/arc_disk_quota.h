// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// DiskQuota - manages the kernel's quota-related operations.

#ifndef CRYPTOHOME_ARC_DISK_QUOTA_H_
#define CRYPTOHOME_ARC_DISK_QUOTA_H_

#include <cstdint>
#include <memory>
#include <string>

#include <base/macros.h>

#include "cryptohome/homedirs.h"
#include "cryptohome/platform.h"
#include "cryptohome/rpc.pb.h"

namespace cryptohome {

// This is the constant that is usually fed to the |home| parameter in
// ArcDiskQuota's constructor.
constexpr char kArcDiskHome[] = "/home";

// Used for constructing the target path for SetProjectId().
constexpr char kUserDownloadsDir[] = "Downloads";
constexpr char kAndroidDataDir[] = "android-data";

// This class handles quota-related query from ARC++, and only designed to be
// called from within the container. The main reason is that IsQuotaSupported
// only makes sense from within the container since it counts the number of
// mounted android-data and only makes sense when the current user's
// android-data is mounted (which depends strictly on the container startup
// sequence: android-data is explicitly mounted before this function is called
// in installd, which is defined in init).
//
// This class only caches the device file that contains the home directory,
// since the device file won't change throughout Cryptohome lifetime. On the
// other hand, IsQuotaSupported is not cached here (please see the comments in
// IsQuotaSupported for the more detailed explanation).
class ArcDiskQuota {
 public:
  // Parameters
  //   homedirs - The mockable Cryptohome homedirs
  //   platform - The mockable Cryptohome platform
  //   home - The path to the home directory, e.g., /home
  ArcDiskQuota(HomeDirs* homedirs,
               Platform* platform,
               const base::FilePath& home);
  ArcDiskQuota(const ArcDiskQuota&) = delete;
  ArcDiskQuota& operator=(const ArcDiskQuota&) = delete;

  virtual ~ArcDiskQuota();

  // Initializing by looking for the right quota mounted device that hosts
  // Android's /data. Not thread-safe.
  virtual void Initialize();

  // Whether or not cryptohome supports quota-based stats. This function returns
  // true when all the following conditions are true:
  // 1. There is a /dev file mounted as /home
  // 2. The dev file above is mounted with quota option enabled
  // 3. There is exactly 1 android-data mounted.
  //
  // Before multiple Android user is supported, make sure to call this
  // function (once) from Android container (i.e., during installd
  // initialization) before asking for curspace. Moreover, this function
  // shouldn't be called too often since it iterates through filesystem and
  // might potentially be expensive.
  //
  // Caching note: This function is intentionally not cached in cryptohome, but
  // should be cached in installd instead, since cryptohome lifetime is
  // different from container's (and the android-data directory). However,
  // caching this during installd's initialization might produce false negative
  // during installd's lifetime. For example in the case when cryptohome
  // concurrently cleans up old users due to low storage event - which might
  // reduce the number of android-data from more than 1 to 1. However, this case
  // should be rare and even if that happens, installd still works correctly
  // using non-quota path.
  // On the other hand, false positive is not desired (since triggering quota
  // path on multiple user will gave undesired result). Fortunately, caching
  // this function in installd won't result to false positive because installd
  // is restarted after everytime android-data is mounted as /data - and hence,
  // there won't be a case where new android-data is mounted in the middle of
  // installd lifetime.
  virtual bool IsQuotaSupported() const;

  // Get the current disk space usage for an android uid (a shifted uid).
  // Returns -1 if quotactl fails.
  virtual int64_t GetCurrentSpaceForUid(uid_t android_uid) const;

  // Get the current disk space usage for an android gid (a shifted gid).
  // Returns -1 if quotactl fails.
  virtual int64_t GetCurrentSpaceForGid(gid_t android_gid) const;

  // Get the current disk space usage for a project ID.
  // Returns -1 if quotactl fails.
  virtual int64_t GetCurrentSpaceForProjectId(int project_id) const;

  // Set the project ID to the file/directory pointed by path.
  // |parent_path|, |child_path| and |obfuscated_username| are used for
  // constructing the target path.
  // Returns true if ioctl succeeds.
  virtual bool SetProjectId(int project_id,
                            SetProjectIdAllowedPathType parent_path,
                            const base::FilePath& child_path,
                            const std::string& obfuscated_username) const;

  // The constants below describes the ranges of valid ID to query (based on
  // what is tracked by installd).These numbers are from
  // system/core/libcutils/include/private/android_filesystem_config.h in
  // Android codebase.

  // The smallest UID in Android that is tracked by installd. This is set to be
  // the minimum possible uid that Android process can have.
  static constexpr uid_t kAndroidUidStart = 0;
  // The largest UID in Android that is tracked by installd. This is from
  // AID_APP_END in android_filesystem_config.h.
  static constexpr uid_t kAndroidUidEnd = 19999;

  // The following section describes the GID that are tracked by installd.
  // Installd tracks different kinds of GID types: Cache, External, Shared, and
  // other Android processes GID that are smaller than Cache GID. The smallest
  // amongst them is 0 and the largest is Shared hence the covered range is
  // between 0 and AID_SHARED_GID_END (inclusive).

  // The smallest GID in Android that is tracked by installd. This is set to be
  // the minimum possible gid that Android process can have.
  static constexpr gid_t kAndroidGidStart = 0;
  // The largest GID in Android that is tracked by installd. This is from
  // AID_SHARED_GID_END in android_filesystem_config.h.
  static constexpr gid_t kAndroidGidEnd = 59999;

 private:
  // Helper function to parse dev file that contains Android's /data.
  base::FilePath GetDevice();

  HomeDirs* homedirs_;
  Platform* platform_;
  const base::FilePath home_;
  base::FilePath device_;

  friend class ArcDiskQuotaTest;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_ARC_DISK_QUOTA_H_
