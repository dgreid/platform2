// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CROS_DISKS_ARCHIVE_MANAGER_H_
#define CROS_DISKS_ARCHIVE_MANAGER_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest_prod.h>

#include "cros-disks/mount_manager.h"

namespace cros_disks {

// A derived class of MountManager for mounting archive files as a virtual
// filesystem.
class ArchiveManager : public MountManager {
 public:
  ArchiveManager(const std::string& mount_root,
                 Platform* platform,
                 Metrics* metrics,
                 brillo::ProcessReaper* process_reaper);
  ~ArchiveManager() override;

  // Initializes the manager and registers default file extensions.
  // Returns true on success.
  bool Initialize() override;

  // Stops a session. Returns true on success.
  bool StopSession() override;

  bool CanUnmount(const std::string& path) const override;

  // Returns true if mounting |source_path| is supported.
  bool CanMount(const std::string& source_path) const override;

  // Returns the type of mount sources supported by the manager.
  MountSourceType GetMountSourceType() const override {
    return MOUNT_SOURCE_ARCHIVE;
  }

  // Registers a set of default archive file extensions to the manager.
  void RegisterDefaultFileExtensions();

  // Registers an archive file extension and the corresponding AVFS handler to
  // to the manager. Subsequent registrations of the same file extension
  // overwrite an existing handler. |extension| should not include the leading
  // dot. |avfs_handler| should be in the form like '#uzip', '#ugz#utar', etc.
  void RegisterFileExtension(const std::string& extension,
                             const std::string& avfs_handler);

  // Allows to deactivate ArchiveManager for RarManager to kick in.
  static void SetActive(bool active) { is_active_ = active; }

 protected:
  // Mounts |source_path| to |mount_path| as |source_format| with |options|.
  // |source_format| can be used to specify the archive file format of
  // |source_path|, so that |source_path| can have any file extension.
  // If |source_format| is an empty string, the archive file format is
  // determined based on the file extension of |source_path|. The underlying
  // mounter may decide to apply mount options different than |options|.
  // |applied_options| is used to return the mount options applied by the
  // mounter.
  std::unique_ptr<MountPoint> DoMount(const std::string& source_path,
                                      const std::string& source_format,
                                      const std::vector<std::string>& options,
                                      const base::FilePath& mount_path,
                                      MountOptions* applied_options,
                                      MountErrorType* error) override;

  // Returns a suggested mount path for a source path.
  std::string SuggestMountPath(const std::string& source_path) const override;

 private:
  // MountPoint implementation that calls RemoveMountVirtualPath() on successful
  // unmount.
  class ArchiveMountPoint;

  // Type definition of a cache mapping a mount path to its source virtual path
  // in the AVFS mount.
  using VirtualPathMap = std::map<std::string, std::string>;

  // Returns the extension of a file, in lower case, at the specified |path|.
  std::string GetFileExtension(const std::string& path) const;

  // Returns the corresponding path inside the AVFS mount of a given |path|
  // with the archive file |extension|, or an empty string if |extension| is
  // not supported or |path| does not have a corresponding path inside the AVFS
  // mount.
  std::string GetAVFSPath(const std::string& path,
                          const std::string& extension) const;

  // Starts AVFS daemons to initialize AVFS mounts. Returns MOUNT_ERROR_NONE on
  // success or if the daemons have already been started.
  MountErrorType StartAVFS();

  // Stops AVFS daemons and unmounts AVFS mounts. Returns true on success or if
  // the AVFS daemons have not yet started.
  bool StopAVFS();

  // Mounts |base_path| to |avfs_path| via AVFS.
  MountErrorType MountAVFSPath(const std::string& base_path,
                               const std::string& avfs_path);

  // Adds a mapping of |mount_path| to |virtual_path| to |virtual_paths_|.
  // An existing mapping of |mount_path| is overwritten.
  void AddMountVirtualPath(const std::string& mount_path,
                           const std::string& virtual_path);

  // Removes a mapping of |mount_path| to its virtual path from
  // |virtual_paths_|. It is a no-op if no such mapping exists.
  void RemoveMountVirtualPath(const std::string& mount_path);

  // Creates directory with given |path| if it does not already exist.
  // Sets its user ID, group ID and permissions.
  bool CreateMountDirectory(const std::string& path) const;

  // A mapping of supported archive file extensions to corresponding AVFS
  // handlers.
  std::map<std::string, std::string> extension_handlers_;

  // A cache mapping a mount path to its source virtual path in the AVFS mount.
  VirtualPathMap virtual_paths_;

  // This variable is set to true if the AVFS daemons have started.
  bool avfs_started_;

  // Mapping of AVFS daemon mount path to MountPoint. Should contain one entry
  // per running AVFS daemon.
  std::map<base::FilePath, std::unique_ptr<MountPoint>> avfsd_mounts_;

  // Allows to deactivate ArchiveManager for RarManager to kick in.
  static bool is_active_;

  FRIEND_TEST(ArchiveManagerTest, GetAVFSPath);
  FRIEND_TEST(ArchiveManagerTest, GetAVFSPathWithInvalidPaths);
  FRIEND_TEST(ArchiveManagerTest, GetAVFSPathWithUnsupportedExtensions);
  FRIEND_TEST(ArchiveManagerTest, GetAVFSPathWithNestedArchives);
  FRIEND_TEST(ArchiveManagerTest, GetFileExtension);
  FRIEND_TEST(ArchiveManagerTest, SuggestMountPath);
  FRIEND_TEST(ArchiveManagerTest, DoMountFailedWithUnsupportedExtension);
  FRIEND_TEST(ArchiveManagerTest,
              CreateMountDirectoryFailsIfCannotCleanLeftOverDirectory);
  FRIEND_TEST(ArchiveManagerTest,
              CreateMountDirectoryFailsIfCannotCreateDirectory);
  FRIEND_TEST(ArchiveManagerTest,
              CreateMountDirectoryFailsIfCannotSetPermissions);
  FRIEND_TEST(ArchiveManagerTest, CreateMountDirectoryFailsIfCannotGetUserId);
  FRIEND_TEST(ArchiveManagerTest,
              CreateMountDirectoryFailsIfCannotSetOwnership);
  FRIEND_TEST(ArchiveManagerTest,
              CreateMountDirectorySucceedsWithoutExistingDirectory);
  FRIEND_TEST(ArchiveManagerTest,
              CreateMountDirectorySucceedsWithExistingDirectory);
};

}  // namespace cros_disks

#endif  // CROS_DISKS_ARCHIVE_MANAGER_H_
