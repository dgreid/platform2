// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/mock_platform.h"

#include "cryptohome/fake_platform.h"

using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;

namespace cryptohome {

MockPlatform::MockPlatform()
    : mock_enumerator_(new NiceMock<MockFileEnumerator>()),
      mock_process_(new NiceMock<brillo::ProcessMock>()),
      fake_platform_(new FakePlatform()) {
  ON_CALL(*this, GetUserId(_, _, _))
      .WillByDefault(Invoke(fake_platform_.get(), &FakePlatform::GetUserId));
  ON_CALL(*this, GetGroupId(_, _))
      .WillByDefault(Invoke(fake_platform_.get(), &FakePlatform::GetGroupId));

  ON_CALL(*this, GetOwnership(_, _, _, _))
      .WillByDefault(Invoke(this, &MockPlatform::MockGetOwnership));
  ON_CALL(*this, SetOwnership(_, _, _, _)).WillByDefault(Return(true));
  ON_CALL(*this, GetPermissions(_, _))
      .WillByDefault(Invoke(this, &MockPlatform::MockGetPermissions));
  ON_CALL(*this, SetPermissions(_, _)).WillByDefault(Return(true));
  ON_CALL(*this, SetGroupAccessible(_, _, _)).WillByDefault(Return(true));
  ON_CALL(*this, GetFileEnumerator(_, _, _))
      .WillByDefault(Invoke(this, &MockPlatform::MockGetFileEnumerator));
  ON_CALL(*this, GetCurrentTime())
      .WillByDefault(Return(base::Time::NowFromSystemTime()));
  ON_CALL(*this, Copy(_, _)).WillByDefault(CallCopy());
  ON_CALL(*this, StatVFS(_, _)).WillByDefault(CallStatVFS());
  ON_CALL(*this, ReportFilesystemDetails(_, _))
      .WillByDefault(CallReportFilesystemDetails());
  ON_CALL(*this, FindFilesystemDevice(_, _))
      .WillByDefault(CallFindFilesystemDevice());
  ON_CALL(*this, DeleteFile(_, _)).WillByDefault(CallDeleteFile());
  ON_CALL(*this, Move(_, _)).WillByDefault(CallMove());
  ON_CALL(*this, EnumerateDirectoryEntries(_, _, _))
      .WillByDefault(CallEnumerateDirectoryEntries());
  ON_CALL(*this, DirectoryExists(_)).WillByDefault(CallDirectoryExists());
  ON_CALL(*this, FileExists(_)).WillByDefault(CallPathExists());
  ON_CALL(*this, CreateDirectory(_)).WillByDefault(CallCreateDirectory());
  ON_CALL(*this, ReadFile(_, _)).WillByDefault(CallReadFile());
  ON_CALL(*this, ReadFileToString(_, _)).WillByDefault(CallReadFileToString());
  ON_CALL(*this, ReadFileToSecureBlob(_, _))
      .WillByDefault(CallReadFileToSecureBlob());
  ON_CALL(*this, Rename(_, _)).WillByDefault(CallRename());
  ON_CALL(*this, ComputeDirectoryDiskUsage(_))
      .WillByDefault(CallComputeDirectoryDiskUsage());
  ON_CALL(*this, SetupProcessKeyring()).WillByDefault(Return(true));
  ON_CALL(*this, GetDirCryptoKeyState(_))
      .WillByDefault(Return(dircrypto::KeyState::NO_KEY));
  ON_CALL(*this, CreateProcessInstance())
      .WillByDefault(Invoke(this, &MockPlatform::MockCreateProcessInstance));
  ON_CALL(*this, AreDirectoriesMounted(_))
      .WillByDefault([](const std::vector<base::FilePath>& directories) {
        return std::vector<bool>(directories.size(), false);
      });
}

MockPlatform::~MockPlatform() {}

}  // namespace cryptohome
