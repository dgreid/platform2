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

  ON_CALL(*this, Rename(_, _))
      .WillByDefault(Invoke(fake_platform_.get(), &FakePlatform::Rename));
  ON_CALL(*this, Move(_, _))
      .WillByDefault(Invoke(fake_platform_.get(), &FakePlatform::Move));
  ON_CALL(*this, Copy(_, _))
      .WillByDefault(Invoke(fake_platform_.get(), &FakePlatform::Copy));
  ON_CALL(*this, DeleteFile(_, _))
      .WillByDefault(Invoke(fake_platform_.get(), &FakePlatform::DeleteFile));
  ON_CALL(*this, DeleteFileDurable(_, _))
      .WillByDefault(
          Invoke(fake_platform_.get(), &FakePlatform::DeleteFileDurable));
  ON_CALL(*this, EnumerateDirectoryEntries(_, _, _))
      .WillByDefault(Invoke(fake_platform_.get(),
                            &FakePlatform::EnumerateDirectoryEntries));
  ON_CALL(*this, FileExists(_))
      .WillByDefault(Invoke(fake_platform_.get(), &FakePlatform::FileExists));
  ON_CALL(*this, DirectoryExists(_))
      .WillByDefault(
          Invoke(fake_platform_.get(), &FakePlatform::DirectoryExists));
  ON_CALL(*this, CreateDirectory(_))
      .WillByDefault(
          Invoke(fake_platform_.get(), &FakePlatform::CreateDirectory));

  ON_CALL(*this, ReadFile(_, _))
      .WillByDefault(Invoke(fake_platform_.get(), &FakePlatform::ReadFile));
  ON_CALL(*this, ReadFileToString(_, _))
      .WillByDefault(
          Invoke(fake_platform_.get(), &FakePlatform::ReadFileToString));
  ON_CALL(*this, ReadFileToSecureBlob(_, _))
      .WillByDefault(
          Invoke(fake_platform_.get(), &FakePlatform::ReadFileToSecureBlob));

  ON_CALL(*this, WriteFile(_, _))
      .WillByDefault(Invoke(fake_platform_.get(), &FakePlatform::WriteFile));
  ON_CALL(*this, WriteSecureBlobToFile(_, _))
      .WillByDefault(
          Invoke(fake_platform_.get(), &FakePlatform::WriteSecureBlobToFile));
  ON_CALL(*this, WriteFileAtomic(_, _, _))
      .WillByDefault(
          Invoke(fake_platform_.get(), &FakePlatform::WriteFileAtomic));
  ON_CALL(*this, WriteSecureBlobToFileAtomic(_, _, _))
      .WillByDefault(Invoke(fake_platform_.get(),
                            &FakePlatform::WriteSecureBlobToFileAtomic));
  ON_CALL(*this, WriteFileAtomicDurable(_, _, _))
      .WillByDefault(
          Invoke(fake_platform_.get(), &FakePlatform::WriteFileAtomicDurable));
  ON_CALL(*this, WriteSecureBlobToFileAtomicDurable(_, _, _))
      .WillByDefault(Invoke(fake_platform_.get(),
                            &FakePlatform::WriteSecureBlobToFileAtomicDurable));
  ON_CALL(*this, WriteStringToFile(_, _))
      .WillByDefault(
          Invoke(fake_platform_.get(), &FakePlatform::WriteStringToFile));
  ON_CALL(*this, WriteStringToFileAtomicDurable(_, _, _))
      .WillByDefault(Invoke(fake_platform_.get(),
                            &FakePlatform::WriteStringToFileAtomicDurable));
  ON_CALL(*this, WriteArrayToFile(_, _, _))
      .WillByDefault(
          Invoke(fake_platform_.get(), &FakePlatform::WriteArrayToFile));

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
  ON_CALL(*this, StatVFS(_, _)).WillByDefault(CallStatVFS());
  ON_CALL(*this, ReportFilesystemDetails(_, _))
      .WillByDefault(CallReportFilesystemDetails());
  ON_CALL(*this, FindFilesystemDevice(_, _))
      .WillByDefault(CallFindFilesystemDevice());
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
