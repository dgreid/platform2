// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// MountHelper objects carry out mount(2) and unmount(2) operations for a single
// cryptohome mount.

#ifndef CRYPTOHOME_MOUNT_HELPER_H_
#define CRYPTOHOME_MOUNT_HELPER_H_

#include <sys/types.h>

#include <memory>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/macros.h>
#include <brillo/process/process.h>
#include <brillo/secure_blob.h>
#include <chromeos/dbus/service_constants.h>

#include "cryptohome/credentials.h"
#include "cryptohome/mount_constants.h"
#include "cryptohome/mount_stack.h"
#include "cryptohome/platform.h"

using base::FilePath;

namespace cryptohome {

extern const char kDefaultHomeDir[];

// Objects that implement MountHelperInterface can perform mount operations.
// This interface will be used as we transition all cryptohome mounts to be
// performed out-of-process.
class MountHelperInterface {
 public:
  virtual ~MountHelperInterface() {}

  struct Options {
    MountType type = MountType::NONE;
    bool to_migrate_from_ecryptfs = false;
    bool shadow_only = false;
  };

  // Ephemeral mounts cannot be performed twice, so cryptohome needs to be able
  // to check whether an ephemeral mount can be performed.
  virtual bool CanPerformEphemeralMount() const = 0;

  // Returns whether an ephemeral mount has been performed.
  virtual bool MountPerformed() const = 0;

  // Returns whether |path| is currently mounted as part of the ephemeral mount.
  virtual bool IsPathMounted(const base::FilePath& path) const = 0;

  // Carries out an ephemeral mount for user |username|.
  virtual bool PerformEphemeralMount(const std::string& username) = 0;

  // Tears down the existing ephemeral mount.
  virtual bool TearDownEphemeralMount() = 0;

  // Carries out mount operations for a regular cryptohome.
  virtual bool PerformMount(const Options& mount_opts,
                            const std::string& username,
                            const std::string& fek_signature,
                            const std::string& fnek_signature,
                            bool is_pristine,
                            MountError* error) = 0;
};

class MountHelper : public MountHelperInterface {
 public:
  MountHelper(uid_t uid,
              gid_t gid,
              gid_t access_gid,
              const base::FilePath& shadow_root,
              const base::FilePath& skel_source,
              const brillo::SecureBlob& system_salt,
              bool legacy_mount,
              Platform* platform)
      : default_uid_(uid),
        default_gid_(gid),
        default_access_gid_(access_gid),
        shadow_root_(shadow_root),
        skeleton_source_(skel_source),
        system_salt_(system_salt),
        legacy_mount_(legacy_mount),
        platform_(platform) {}
  ~MountHelper() = default;

  // Returns the temporary user path while we're migrating for
  // http://crbug.com/224291.
  static base::FilePath GetNewUserPath(const std::string& username);

  // Returns the path to sparse file used for ephemeral cryptohome for the user.
  static FilePath GetEphemeralSparseFile(
      const std::string& obfuscated_username);

  // Ensures that root and user mountpoints for the specified user are present.
  // Returns false if the mountpoints were not present and could not be created.
  bool EnsureUserMountPoints(const std::string& username) const;

  // Gets the directory to temporarily mount the user's cryptohome at.
  //
  // Parameters
  //   obfuscated_username - Obfuscated username field of the credentials.
  FilePath GetUserTemporaryMountDirectory(
      const std::string& obfuscated_username) const;

  // Creates the tracked subdirectories in a user's cryptohome.
  // If the cryptohome did not have tracked directories, but had them untracked,
  // migrate their contents.
  //
  // Parameters
  //   obfuscated_username - The obfuscated form of the username
  //   type - Mount type: eCryptfs or dircrypto
  //   is_pristine - True, if the cryptohome is being created
  bool CreateTrackedSubdirectories(const std::string& obfuscated_username,
                                   const MountType& type,
                                   bool is_pristine) const;

  // Carries out eCryptfs/dircrypto mount(2) operations for a regular
  // cryptohome.
  bool PerformMount(const Options& mount_opts,
                    const std::string& username,
                    const std::string& fek_signature,
                    const std::string& fnek_signature,
                    bool is_pristine,
                    MountError* error) override;

  // Carries out dircrypto mount(2) operations for an ephemeral cryptohome.
  // Does not clean up on failure.
  bool PerformEphemeralMount(const std::string& username) override;

  // Tears down an ephemeral cryptohome mount in-process by calling umount(2).
  bool TearDownEphemeralMount() override;

  // Unmounts all mount points.
  // Relies on ForceUnmount() internally; see the caveat listed for it.
  void UnmountAll();

  // Deletes loop device used for ephemeral cryptohome and underlying temporary
  // sparse file.
  bool CleanUpEphemeral();

  // Returns whether an ephemeral mount operation can be performed.
  bool CanPerformEphemeralMount() const override;

  // Returns whether a mount operation has been performed.
  bool MountPerformed() const override;

  // Returns whether |path| is the destination of an existing mount.
  bool IsPathMounted(const base::FilePath& path) const override;

  // Returns a list of paths that have been mounted as part of the mount.
  std::vector<base::FilePath> MountedPaths() const;

 private:
  // Returns the names of all tracked subdirectories.
  static std::vector<base::FilePath> GetTrackedSubdirectories();

  // Returns the mounted userhome path (e.g. /home/.shadow/.../mount/user)
  //
  // Parameters
  //   obfuscated_username - Obfuscated username field of the credentials.
  FilePath GetMountedUserHomePath(const std::string& obfuscated_username) const;

  // Returns the mounted roothome path (e.g. /home/.shadow/.../mount/root)
  //
  // Parameters
  //   obfuscated_username - Obfuscated username field of the credentials.
  FilePath GetMountedRootHomePath(
      const std::string& obfuscated_username) const;

  // Mounts a mount point and pushes it to the mount stack.
  // Returns true if the mount succeeds, false otherwise.
  //
  // Parameters
  //   src - Path to mount from
  //   dest - Path to mount to
  //   type - Filesystem type to mount with
  //   options - Filesystem options to supply
  bool MountAndPush(const base::FilePath& src,
                    const base::FilePath& dest,
                    const std::string& type,
                    const std::string& options);

  // Binds a mount point, remembering it for later unmounting.
  // Returns true if the bind succeeds, false otherwise.
  //
  // Parameters
  //   src - Path to bind from
  //   dest - Path to bind to
  bool BindAndPush(const FilePath& src, const FilePath& dest);

  // Bind mounts |user_home|/Downloads to |user_home|/MyFiles/Downloads so Files
  // app can manage MyFiles as user volume instead of just Downloads.
  bool BindMyFilesDownloads(const base::FilePath& user_home);

  // Copies the skeleton directory to the user's cryptohome.
  void CopySkeleton(const FilePath& destination) const;

  // Ensures that a specified directory exists, with all path components but the
  // last one owned by kMountOwnerUid:kMountOwnerGid and the last component
  // owned by desired_uid:desired_gid.
  //
  // Parameters
  //   dir - Directory to check
  //   desired_uid - uid that must own the directory
  //   desired_gid - gid that muts own the directory
  bool EnsureDirHasOwner(const base::FilePath& dir,
                         uid_t desired_uid,
                         gid_t desired_gid) const;

  // Ensures that the |num|th component of |path| is owned by |uid|:|gid| and is
  // a directory.
  bool EnsurePathComponent(const FilePath& path,
                           size_t num,
                           uid_t uid,
                           gid_t gid) const;

  // Ensures that the permissions on every parent of /home/chronos/u-$hash are
  // correct and that they are all directories. Since we're going to bind-mount
  // over the directory, we don't care what the permissions on it are, just that
  // it exists.
  // /home needs to be root:root.
  // /home/chronos needs to be default_uid_:default_gid_.
  bool EnsureNewUserDirExists(const std::string& username) const;

  // Attempts to unmount a mountpoint. If the unmount fails, logs processes with
  // open handles to it and performs a lazy unmount.
  //
  // Parameters
  //   src - Path mounted at |dest|
  //   dest - Mount point to unmount
  void ForceUnmount(const base::FilePath& src, const base::FilePath& dest);

  // Migrates from the home-in-encfs setup to the home-in-subdir setup. Instead
  // of storing all the user's files in the root of the encfs, we store them in
  // a subdirectory of it to make room for a root-owned, user-encrypted volume.
  //
  // Parameters
  //   vault_path - directory to migrate
  void MigrateToUserHome(const FilePath& vault_path) const;

  // Bind-mounts
  //   /home/.shadow/$hash/mount/root/$daemon (*)
  // to
  //   /run/daemon-store/$daemon/$hash
  // for a hardcoded list of $daemon directories.
  //
  // This can be used to make the Cryptohome mount propagate into the daemon's
  // mount namespace. See
  // https://chromium.googlesource.com/chromiumos/docs/+/master/sandboxing.md#securely-mounting-cryptohome-daemon-store-folders
  // for details.
  //
  // (*) Path for a regular mount. The path is different for an ephemeral mount.
  bool MountDaemonStoreDirectories(const FilePath& root_home,
                                   const std::string& obfuscated_username);

  // Sets up bind mounts from |user_home| and |root_home| to
  //   - /home/chronos/user (see MountLegacyHome()),
  //   - /home/chronos/u-<user_hash>,
  //   - /home/user/<user_hash>,
  //   - /home/root/<user_hash> and
  //   - /run/daemon-store/$daemon/<user_hash>
  //     (see MountDaemonStoreDirectories()).
  // The parameters have the same meaning as in MountCryptohomeInner resp.
  // MountEphemeralCryptohomeInner. Returns true if successful, false otherwise.
  bool MountHomesAndDaemonStores(const std::string& username,
                                 const std::string& obfuscated_username,
                                 const FilePath& user_home,
                                 const FilePath& root_home);

  // Mounts the legacy home directory.
  // The legacy home directory is from before multiprofile and is mounted at
  // /home/chronos/user.
  bool MountLegacyHome(const FilePath& from);

  // Creates a loop device formatted as an ext4 partition.
  bool PrepareEphemeralDevice(const std::string& obfuscated_username);

  // Recursively copies directory contents to the destination if the destination
  // file does not exist.  Sets ownership to |default_user_|.
  //
  // Parameters
  //   source - Where to copy files from
  //   destination - Where to copy files to
  void RecursiveCopy(const FilePath& source, const FilePath& destination) const;

  // Sets up a freshly mounted ephemeral cryptohome by adjusting its permissions
  // and populating it with a skeleton directory and file structure.
  bool SetUpEphemeralCryptohome(const FilePath& source_path);

  // Changes the group ownership and permissions on those directories inside
  // the cryptohome that need to be accessible by other system daemons.
  bool SetUpGroupAccess(const FilePath& home_dir) const;

  uid_t default_uid_;
  uid_t default_gid_;
  uid_t default_access_gid_;

  // Where to store the system salt and user salt/key/vault. Defaults to
  // /home/.shadow
  base::FilePath shadow_root_;

  // Where the skeleton for the user's cryptohome is copied from.
  base::FilePath skeleton_source_;

  // Stores the global system salt.
  brillo::SecureBlob system_salt_;

  bool legacy_mount_ = true;

  // Stack of mounts (in the mount(2) sense) that have been made.
  MountStack stack_;

  // Tracks loop device used for ephemeral cryptohome.
  // Empty when the device is not present.
  base::FilePath ephemeral_loop_device_;

  // Tracks path to ephemeral cryptohome sparse file.
  // Empty when the file is not created or already deleted.
  base::FilePath ephemeral_file_path_;

  Platform* platform_;  // Un-owned.

  FRIEND_TEST(MountTest, BindMyFilesDownloadsSuccess);
  FRIEND_TEST(MountTest, BindMyFilesDownloadsMissingUserHome);
  FRIEND_TEST(MountTest, BindMyFilesDownloadsMissingDownloads);
  FRIEND_TEST(MountTest, BindMyFilesDownloadsMissingMyFilesDownloads);

  FRIEND_TEST(MountTest, CreateTrackedSubdirectories);
  FRIEND_TEST(MountTest, CreateTrackedSubdirectoriesReplaceExistingDir);

  FRIEND_TEST(MountTest, RememberMountOrderingTest);

  FRIEND_TEST(EphemeralNoUserSystemTest, CreateMyFilesDownloads);
  FRIEND_TEST(EphemeralNoUserSystemTest, CreateMyFilesDownloadsAlreadyExists);

  DISALLOW_COPY_AND_ASSIGN(MountHelper);
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_MOUNT_HELPER_H_
