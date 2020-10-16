// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_MOCK_PLATFORM_H_
#define CRYPTOHOME_MOCK_PLATFORM_H_

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/time/time.h>
#include <brillo/process/process_mock.h>
#include <gmock/gmock.h>

#include "cryptohome/platform.h"

namespace cryptohome {

class MockFileEnumerator : public FileEnumerator {
 public:
  MockFileEnumerator() {
    ON_CALL(*this, Next())
        .WillByDefault(::testing::Invoke(this, &MockFileEnumerator::MockNext));
    ON_CALL(*this, GetInfo())
        .WillByDefault(
            ::testing::Invoke(this, &MockFileEnumerator::MockGetInfo));
  }
  virtual ~MockFileEnumerator() {}

  void AddFileEntry(base::FilePath path) {
    // Add a placeholder struct stat.
    base::stat_wrapper_t s;
    entries_.emplace_back(FileInfo(path, s));
  }

  MOCK_METHOD(base::FilePath, Next, (), (override));
  MOCK_METHOD(FileInfo, GetInfo, (), (override));

  std::vector<FileInfo> entries_;

 protected:
  virtual base::FilePath MockNext() {
    if (entries_.empty())
      return base::FilePath();
    current_ = entries_.at(0);
    entries_.erase(entries_.begin(), entries_.begin() + 1);
    return current_.GetName();
  }
  virtual const FileInfo& MockGetInfo() { return current_; }
  FileInfo current_;
};

// TODO(wad) Migrate to an in-memory-only mock filesystem.
ACTION(CallDeleteFile) {
  return base::DeleteFile(base::FilePath(arg0), arg1);
}
ACTION(CallMove) {
  return base::Move(base::FilePath(arg0), base::FilePath(arg1));
}
ACTION(CallEnumerateDirectoryEntries) {
  // Pass a call to EnumerateDirectoryEntries through to a real Platform if it's
  // not mocked.
  Platform p;
  return p.EnumerateDirectoryEntries(arg0, arg1, arg2);
}
ACTION(CallDirectoryExists) {
  return base::DirectoryExists(base::FilePath(arg0));
}
ACTION(CallPathExists) {
  return base::PathExists(base::FilePath(arg0));
}
ACTION(CallCreateDirectory) {
  return base::CreateDirectory(base::FilePath(arg0));
}
ACTION(CallReadFile) {
  return Platform().ReadFile(arg0, arg1);
}
ACTION(CallReadFileToString) {
  return Platform().ReadFileToString(arg0, arg1);
}
ACTION(CallReadFileToSecureBlob) {
  return Platform().ReadFileToSecureBlob(arg0, arg1);
}
ACTION(CallCopy) {
  return Platform().Copy(arg0, arg1);
}
ACTION(CallRename) {
  return Platform().Rename(arg0, arg1);
}
ACTION(CallComputeDirectoryDiskUsage) {
  return Platform().ComputeDirectoryDiskUsage(arg0);
}
ACTION(CallStatVFS) {
  return Platform().StatVFS(arg0, arg1);
}
ACTION(CallReportFilesystemDetails) {
  return Platform().ReportFilesystemDetails(arg0, arg1);
}
ACTION(CallFindFilesystemDevice) {
  return Platform().FindFilesystemDevice(arg0, arg1);
}

class MockPlatform : public Platform {
 public:
  MockPlatform();
  virtual ~MockPlatform();
  MOCK_METHOD(bool,
              Mount,
              (const base::FilePath&,
               const base::FilePath&,
               const std::string&,
               uint32_t,
               const std::string&),
              (override));
  MOCK_METHOD(bool,
              Bind,
              (const base::FilePath&, const base::FilePath&),
              (override));
  MOCK_METHOD(bool, Unmount, (const base::FilePath&, bool, bool*), (override));
  MOCK_METHOD(void, LazyUnmount, (const base::FilePath&), (override));
  MOCK_METHOD(bool,
              GetLoopDeviceMounts,
              ((std::multimap<const base::FilePath, const base::FilePath>*)),
              (override));
  MOCK_METHOD(bool,
              GetMountsBySourcePrefix,
              (const base::FilePath&,
               (std::multimap<const base::FilePath, const base::FilePath>*)),
              (override));
  MOCK_METHOD(bool, IsDirectoryMounted, (const base::FilePath&), (override));
  MOCK_METHOD(base::Optional<std::vector<bool>>,
              AreDirectoriesMounted,
              (const std::vector<base::FilePath>&),
              (override));
  MOCK_METHOD(std::unique_ptr<brillo::Process>,
              CreateProcessInstance,
              (),
              (override));
  MOCK_METHOD(void,
              GetProcessesWithOpenFiles,
              (const base::FilePath&, std::vector<ProcessInformation>*),
              (override));
  MOCK_METHOD(bool,
              GetOwnership,
              (const base::FilePath&, uid_t*, gid_t*, bool),
              (const, override));
  MOCK_METHOD(bool,
              SetOwnership,
              (const base::FilePath&, uid_t, gid_t, bool),
              (const, override));
  MOCK_METHOD(bool,
              GetPermissions,
              (const base::FilePath&, mode_t*),
              (const, override));
  MOCK_METHOD(bool,
              SetPermissions,
              (const base::FilePath&, mode_t),
              (const, override));
  MOCK_METHOD(bool,
              SetGroupAccessible,
              (const base::FilePath&, gid_t group_id, mode_t group_mode),
              (const, override));
  MOCK_METHOD(bool,
              GetUserId,
              (const std::string&, uid_t*, gid_t*),
              (const, override));
  MOCK_METHOD(bool,
              GetGroupId,
              (const std::string&, gid_t*),
              (const, override));
  MOCK_METHOD(int64_t,
              AmountOfFreeDiskSpace,
              (const base::FilePath&),
              (const, override));
  MOCK_METHOD(int64_t,
              GetQuotaCurrentSpaceForUid,
              (const base::FilePath&, uid_t),
              (const, override));
  MOCK_METHOD(int64_t,
              GetQuotaCurrentSpaceForGid,
              (const base::FilePath&, gid_t),
              (const, override));
  MOCK_METHOD(bool, FileExists, (const base::FilePath&), (override));
  MOCK_METHOD(int, Access, (const base::FilePath&, uint32_t), (override));
  MOCK_METHOD(bool, GetFileSize, (const base::FilePath&, int64_t*), (override));
  MOCK_METHOD(int64_t,
              ComputeDirectoryDiskUsage,
              (const base::FilePath&),
              (override));
  MOCK_METHOD(FILE*,
              OpenFile,
              (const base::FilePath&, const char*),
              (override));
  MOCK_METHOD(void,
              InitializeFile,
              (base::File*, const base::FilePath&, uint32_t),
              (override));
  MOCK_METHOD(bool, LockFile, (int), (override));
  MOCK_METHOD(bool,
              CloseFile,
              (FILE*),
              (override));  // NOLINT(readability/function)
  MOCK_METHOD(FILE*, CreateAndOpenTemporaryFile, (base::FilePath*), (override));
  MOCK_METHOD(bool,
              Stat,
              (const base::FilePath&, base::stat_wrapper_t*),
              (override));
  MOCK_METHOD(bool,
              HasExtendedFileAttribute,
              (const base::FilePath&, const std::string&),
              (override));
  MOCK_METHOD(bool,
              ListExtendedFileAttributes,
              (const base::FilePath&, std::vector<std::string>*),
              (override));
  MOCK_METHOD(bool,
              GetExtendedFileAttributeAsString,
              (const base::FilePath&, const std::string&, std::string*),
              (override));
  MOCK_METHOD(bool,
              GetExtendedFileAttribute,
              (const base::FilePath&, const std::string&, char*, ssize_t),
              (override));
  MOCK_METHOD(bool,
              SetExtendedFileAttribute,
              (const base::FilePath&, const std::string&, const char*, size_t),
              (override));
  MOCK_METHOD(bool,
              RemoveExtendedFileAttribute,
              (const base::FilePath&, const std::string&),
              (override));
  MOCK_METHOD(bool,
              GetExtFileAttributes,
              (const base::FilePath&, int*),
              (override));
  MOCK_METHOD(bool,
              SetExtFileAttributes,
              (const base::FilePath&, int),
              (override));
  MOCK_METHOD(bool,
              HasNoDumpFileAttribute,
              (const base::FilePath&),
              (override));
  MOCK_METHOD(bool,
              ReadFile,
              (const base::FilePath&, brillo::Blob*),
              (override));
  MOCK_METHOD(bool,
              ReadFileToString,
              (const base::FilePath&, std::string*),
              (override));
  MOCK_METHOD(bool,
              ReadFileToSecureBlob,
              (const base::FilePath&, brillo::SecureBlob*),
              (override));
  MOCK_METHOD(bool,
              Rename,
              (const base::FilePath&, const base::FilePath&),
              (override));
  MOCK_METHOD(bool, WriteOpenFile, (FILE*, const brillo::Blob&), (override));
  MOCK_METHOD(bool,
              WriteFile,
              (const base::FilePath&, const brillo::Blob&),
              (override));
  MOCK_METHOD(bool,
              WriteSecureBlobToFile,
              (const base::FilePath&, const brillo::SecureBlob&),
              (override));
  MOCK_METHOD(bool,
              WriteFileAtomic,
              (const base::FilePath&, const brillo::Blob&, mode_t mode),
              (override));
  MOCK_METHOD(bool,
              WriteSecureBlobToFileAtomic,
              (const base::FilePath&, const brillo::SecureBlob&, mode_t mode),
              (override));
  MOCK_METHOD(bool,
              WriteFileAtomicDurable,
              (const base::FilePath&, const brillo::Blob&, mode_t mode),
              (override));
  MOCK_METHOD(bool,
              WriteSecureBlobToFileAtomicDurable,
              (const base::FilePath&, const brillo::SecureBlob&, mode_t mode),
              (override));
  MOCK_METHOD(bool,
              WriteStringToFile,
              (const base::FilePath&, const std::string&),
              (override));
  MOCK_METHOD(bool,
              WriteStringToFileAtomicDurable,
              (const base::FilePath&, const std::string&, mode_t mode),
              (override));
  MOCK_METHOD(bool,
              WriteArrayToFile,
              (const base::FilePath& path, const char* data, size_t size),
              (override));
  MOCK_METHOD(bool, TouchFileDurable, (const base::FilePath& path), (override));
  MOCK_METHOD(base::Time, GetCurrentTime, (), (const, override));
  MOCK_METHOD(bool,
              Copy,
              (const base::FilePath&, const base::FilePath&),
              (override));
  MOCK_METHOD(bool,
              Move,
              (const base::FilePath&, const base::FilePath&),
              (override));
  MOCK_METHOD(bool,
              StatVFS,
              (const base::FilePath&, struct statvfs*),
              (override));
  MOCK_METHOD(bool,
              SameVFS,
              (const base::FilePath&, const base::FilePath&),
              (override));
  MOCK_METHOD(bool,
              ReportFilesystemDetails,
              (const base::FilePath&, const base::FilePath&),
              (override));
  MOCK_METHOD(bool,
              FindFilesystemDevice,
              (const base::FilePath&, std::string*),
              (override));
  MOCK_METHOD(bool,
              EnumerateDirectoryEntries,
              (const base::FilePath&, bool, std::vector<base::FilePath>*),
              (override));
  MOCK_METHOD(bool, DeleteFile, (const base::FilePath&, bool), (override));
  MOCK_METHOD(bool,
              DeleteFileDurable,
              (const base::FilePath&, bool),
              (override));
  MOCK_METHOD(bool, DirectoryExists, (const base::FilePath&), (override));
  MOCK_METHOD(bool, CreateDirectory, (const base::FilePath&), (override));
  MOCK_METHOD(bool, SetupProcessKeyring, (), (override));
  MOCK_METHOD(dircrypto::KeyState,
              GetDirCryptoKeyState,
              (const base::FilePath&),
              (override));
  MOCK_METHOD(bool,
              SetDirCryptoKey,
              (const base::FilePath&, const dircrypto::KeyReference&),
              (override));
  MOCK_METHOD(bool,
              AddDirCryptoKeyToKeyring,
              (const brillo::SecureBlob& key, dircrypto::KeyReference*),
              (override));
  MOCK_METHOD(bool,
              InvalidateDirCryptoKey,
              (const dircrypto::KeyReference&, const base::FilePath&),
              (override));
  MOCK_METHOD(bool, ClearUserKeyring, (), (override));
  MOCK_METHOD(bool,
              AddEcryptfsAuthToken,
              (const brillo::SecureBlob&,
               const std::string&,
               const brillo::SecureBlob&),
              (override));
  MOCK_METHOD(FileEnumerator*,
              GetFileEnumerator,
              (const base::FilePath&, bool, int),
              (override));
  MOCK_METHOD(bool, FirmwareWriteProtected, (), (override));
  MOCK_METHOD(bool, DataSyncFile, (const base::FilePath&), (override));
  MOCK_METHOD(bool, SyncFile, (const base::FilePath&), (override));
  MOCK_METHOD(bool, SyncDirectory, (const base::FilePath&), (override));
  MOCK_METHOD(void, Sync, (), (override));
  MOCK_METHOD(std::string, GetHardwareID, (), (override));
  MOCK_METHOD(bool,
              CreateSymbolicLink,
              (const base::FilePath&, const base::FilePath&),
              (override));
  MOCK_METHOD(bool,
              ReadLink,
              (const base::FilePath&, base::FilePath*),
              (override));
  MOCK_METHOD(bool,
              SetFileTimes,
              (const base::FilePath&,
               const struct timespec&,
               const struct timespec&,
               bool),
              (override));
  MOCK_METHOD(bool, SendFile, (int, int, off_t, size_t), (override));
  MOCK_METHOD(bool,
              CreateSparseFile,
              (const base::FilePath&, int64_t),
              (override));
  MOCK_METHOD(bool, GetBlkSize, (const base::FilePath&, uint64_t*), (override));
  MOCK_METHOD(base::FilePath, AttachLoop, (const base::FilePath&), (override));
  MOCK_METHOD(bool, DetachLoop, (const base::FilePath&), (override));
  MOCK_METHOD(std::vector<LoopDevice>, GetAttachedLoopDevices, (), (override));
  MOCK_METHOD(bool,
              FormatExt4,
              (const base::FilePath&,
               const std::vector<std::string>&,
               uint64_t),
              (override));
  MOCK_METHOD(bool,
              ResizeFilesystem,
              (const base::FilePath&, uint64_t),
              (override));
  MOCK_METHOD(bool,
              RestoreSELinuxContexts,
              (const base::FilePath&, bool),
              (override));

  MockFileEnumerator* mock_enumerator() { return mock_enumerator_.get(); }
  brillo::ProcessMock* mock_process() { return mock_process_.get(); }

 private:
  std::unique_ptr<brillo::Process> MockCreateProcessInstance() {
    auto res = std::move(mock_process_);
    mock_process_ =
        std::make_unique<::testing::NiceMock<brillo::ProcessMock>>();
    return res;
  }

  bool MockGetOwnership(const base::FilePath& path,
                        uid_t* user_id,
                        gid_t* group_id,
                        bool dereference_links) const {
    *user_id = getuid();
    *group_id = getgid();
    return true;
  }

  bool MockGetPermissions(const base::FilePath& path, mode_t* mode) const {
    *mode = S_IRWXU | S_IRGRP | S_IXGRP;
    return true;
  }

  bool MockGetUserId(const std::string& user, uid_t* user_id, gid_t* group_id) {
    *user_id = getuid();
    *group_id = getgid();
    return true;
  }

  bool MockGetGroupId(const std::string& group, gid_t* group_id) {
    *group_id = getgid();
    return true;
  }

  FileEnumerator* MockGetFileEnumerator(const base::FilePath& root_path,
                                        bool recursive,
                                        int file_type) {
    MockFileEnumerator* e = mock_enumerator_.release();
    mock_enumerator_.reset(new ::testing::NiceMock<MockFileEnumerator>());
    mock_enumerator_->entries_.assign(e->entries_.begin(), e->entries_.end());
    return e;
  }

  std::unique_ptr<MockFileEnumerator> mock_enumerator_;
  std::unique_ptr<brillo::ProcessMock> mock_process_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_MOCK_PLATFORM_H_
