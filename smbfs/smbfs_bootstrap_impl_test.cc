// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "smbfs/smbfs_bootstrap_impl.h"

#include <memory>
#include <utility>
#include <vector>

#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/message_loop/message_loop.h>
#include <base/run_loop.h>
#include <base/test/bind_test_util.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <mojo/core/embedder/embedder.h>
#include <mojo/public/cpp/system/platform_handle.h>

#include "smbfs/smb_filesystem.h"

namespace smbfs {
namespace {

using ::testing::_;
using ::testing::Return;
using ::testing::WithArg;

const char kSharePath[] = "smb://server/share";
const char kWorkgroup[] = "my-workgroup";
const char kUsername[] = "my-username";
const char kPassword[] = "my-super-secret-password";
const char kKerberosGuid[] = "1234-5678-my-guid";

class MockSmbFilesystem : public SmbFilesystem {
 public:
  MockSmbFilesystem() : SmbFilesystem(kSharePath) {}

  MOCK_METHOD(ConnectError, EnsureConnected, (), (override));
  MOCK_METHOD(void,
              SetResolvedAddress,
              (const std::vector<uint8_t>&),
              (override));
};

class MockBootstrapDelegate : public SmbFsBootstrapImpl::Delegate {
 public:
  MOCK_METHOD(void,
              SetupKerberos,
              (mojom::KerberosConfigPtr,
               base::OnceCallback<void(bool success)>),
              (override));
  MOCK_METHOD(std::unique_ptr<SmbFilesystem>,
              CreateSmbFilesystem,
              (const std::string&, std::unique_ptr<SmbCredential>),
              (override));
  MOCK_METHOD(bool,
              StartFuseSession,
              (std::unique_ptr<Filesystem>),
              (override));
  MOCK_METHOD(void, OnBootstrapConnectionError, (), (override));
};

class MockSmbFsDelegate : public mojom::SmbFsDelegate {
 public:
  explicit MockSmbFsDelegate(mojom::SmbFsDelegateRequest request)
      : binding_(this, std::move(request)) {}

 private:
  mojo::Binding<mojom::SmbFsDelegate> binding_;
};

mojom::PasswordPtr MakePasswordOption(const std::string& password) {
  mojom::PasswordPtr password_option = mojom::Password::New();
  password_option->length = password.size();

  int fds[2];
  CHECK(base::CreateLocalNonBlockingPipe(fds));
  base::ScopedFD read_fd(fds[0]);
  base::ScopedFD write_fd(fds[1]);

  EXPECT_TRUE(base::WriteFileDescriptor(write_fd.get(), password.c_str(),
                                        password.size()));
  password_option->fd =
      mojo::WrapPlatformHandle(mojo::PlatformHandle(std::move(read_fd)));

  return password_option;
}

class TestSmbFsBootstrapImpl : public testing::Test {
 public:
  void SetUp() override {
    mojo::core::Init();

    mock_smbfs_delegate_ = std::make_unique<MockSmbFsDelegate>(
        mojo::MakeRequest(&smbfs_delegate_ptr_));
  }

 protected:
  base::MessageLoopForIO message_loop_;
  MockBootstrapDelegate mock_delegate_;

  mojom::SmbFsDelegatePtr smbfs_delegate_ptr_;
  std::unique_ptr<MockSmbFsDelegate> mock_smbfs_delegate_;
};

TEST_F(TestSmbFsBootstrapImpl, GuestAuth) {
  mojom::SmbFsBootstrapPtr boostrap_ptr;
  SmbFsBootstrapImpl boostrap_impl(mojo::MakeRequest(&boostrap_ptr),
                                   &mock_delegate_);

  EXPECT_CALL(mock_delegate_, CreateSmbFilesystem(kSharePath, _))
      .WillOnce(WithArg<1>([](std::unique_ptr<SmbCredential> credential) {
        EXPECT_TRUE(credential->workgroup.empty());
        EXPECT_TRUE(credential->username.empty());
        EXPECT_FALSE(credential->password);

        std::unique_ptr<MockSmbFilesystem> fs =
            std::make_unique<MockSmbFilesystem>();
        EXPECT_CALL(*fs, EnsureConnected())
            .WillOnce(Return(SmbFilesystem::ConnectError::kOk));
        EXPECT_CALL(*fs, SetResolvedAddress(std::vector<uint8_t>({1, 2, 3, 4})))
            .Times(1);
        return fs;
      }));
  std::unique_ptr<Filesystem> captured_fs;
  EXPECT_CALL(mock_delegate_, StartFuseSession(_))
      .WillOnce([&captured_fs](std::unique_ptr<Filesystem> fs) {
        // Capture the filesystem to avoid tearing down the SmbFs Mojo service.
        captured_fs = std::move(fs);
        return true;
      });

  mojom::MountOptionsPtr mount_options = mojom::MountOptions::New();
  mount_options->share_path = kSharePath;
  mount_options->resolved_host =
      mojom::IPAddress::New(std::vector<uint8_t>({1, 2, 3, 4}));

  base::RunLoop run_loop;
  boostrap_ptr->MountShare(
      std::move(mount_options), std::move(smbfs_delegate_ptr_),
      base::BindLambdaForTesting([&run_loop](mojom::MountError mount_error,
                                             mojom::SmbFsPtr smbfs_ptr) {
        EXPECT_EQ(mojom::MountError::kOk, mount_error);
        EXPECT_TRUE(smbfs_ptr);
        run_loop.Quit();
      }));
  run_loop.Run();
}

TEST_F(TestSmbFsBootstrapImpl, UsernamePasswordAuth) {
  mojom::SmbFsBootstrapPtr boostrap_ptr;
  SmbFsBootstrapImpl boostrap_impl(mojo::MakeRequest(&boostrap_ptr),
                                   &mock_delegate_);

  EXPECT_CALL(mock_delegate_, CreateSmbFilesystem(kSharePath, _))
      .WillOnce(WithArg<1>([](std::unique_ptr<SmbCredential> credential) {
        EXPECT_EQ(credential->workgroup, kWorkgroup);
        EXPECT_EQ(credential->username, kUsername);
        EXPECT_EQ(credential->password->GetRaw(), std::string(kPassword));

        std::unique_ptr<MockSmbFilesystem> fs =
            std::make_unique<MockSmbFilesystem>();
        EXPECT_CALL(*fs, EnsureConnected())
            .WillOnce(Return(SmbFilesystem::ConnectError::kOk));
        EXPECT_CALL(*fs, SetResolvedAddress(_)).Times(0);
        return fs;
      }));
  std::unique_ptr<Filesystem> captured_fs;
  EXPECT_CALL(mock_delegate_, StartFuseSession(_))
      .WillOnce([&captured_fs](std::unique_ptr<Filesystem> fs) {
        // Capture the filesystem to avoid tearing down the SmbFs Mojo service.
        captured_fs = std::move(fs);
        return true;
      });

  mojom::MountOptionsPtr mount_options = mojom::MountOptions::New();
  mount_options->share_path = kSharePath;
  mount_options->workgroup = kWorkgroup;
  mount_options->username = kUsername;
  mount_options->password = MakePasswordOption(kPassword);

  base::RunLoop run_loop;
  boostrap_ptr->MountShare(
      std::move(mount_options), std::move(smbfs_delegate_ptr_),
      base::BindLambdaForTesting([&run_loop](mojom::MountError mount_error,
                                             mojom::SmbFsPtr smbfs_ptr) {
        EXPECT_EQ(mojom::MountError::kOk, mount_error);
        EXPECT_TRUE(smbfs_ptr);
        run_loop.Quit();
      }));
  run_loop.Run();
}

TEST_F(TestSmbFsBootstrapImpl, KerberosAuth) {
  mojom::SmbFsBootstrapPtr boostrap_ptr;
  SmbFsBootstrapImpl boostrap_impl(mojo::MakeRequest(&boostrap_ptr),
                                   &mock_delegate_);

  EXPECT_CALL(mock_delegate_, SetupKerberos(_, _))
      .WillOnce([](mojom::KerberosConfigPtr config,
                   base::OnceCallback<void(bool success)> callback) {
        EXPECT_EQ(config->source, mojom::KerberosConfig::Source::kKerberos);
        EXPECT_EQ(config->identity, kKerberosGuid);
        std::move(callback).Run(true);
      });
  EXPECT_CALL(mock_delegate_, CreateSmbFilesystem(kSharePath, _))
      .WillOnce(WithArg<1>([](std::unique_ptr<SmbCredential> credential) {
        EXPECT_EQ(credential->workgroup, kWorkgroup);
        EXPECT_EQ(credential->username, kUsername);
        EXPECT_FALSE(credential->password);

        std::unique_ptr<MockSmbFilesystem> fs =
            std::make_unique<MockSmbFilesystem>();
        EXPECT_CALL(*fs, EnsureConnected())
            .WillOnce(Return(SmbFilesystem::ConnectError::kOk));
        EXPECT_CALL(*fs, SetResolvedAddress(_)).Times(0);
        return fs;
      }));
  std::unique_ptr<Filesystem> captured_fs;
  EXPECT_CALL(mock_delegate_, StartFuseSession(_))
      .WillOnce([&captured_fs](std::unique_ptr<Filesystem> fs) {
        // Capture the filesystem to avoid tearing down the SmbFs Mojo service.
        captured_fs = std::move(fs);
        return true;
      });

  mojom::MountOptionsPtr mount_options = mojom::MountOptions::New();
  mount_options->share_path = kSharePath;
  mount_options->workgroup = kWorkgroup;
  mount_options->username = kUsername;
  mount_options->kerberos_config = mojom::KerberosConfig::New(
      mojom::KerberosConfig::Source::kKerberos, kKerberosGuid);
  // These two options will be ignored when Kerberos is being used.
  mount_options->password = MakePasswordOption(kPassword);
  mount_options->resolved_host =
      mojom::IPAddress::New(std::vector<uint8_t>({1, 2, 3, 4}));

  base::RunLoop run_loop;
  boostrap_ptr->MountShare(
      std::move(mount_options), std::move(smbfs_delegate_ptr_),
      base::BindLambdaForTesting([&run_loop](mojom::MountError mount_error,
                                             mojom::SmbFsPtr smbfs_ptr) {
        EXPECT_EQ(mojom::MountError::kOk, mount_error);
        EXPECT_TRUE(smbfs_ptr);
        run_loop.Quit();
      }));
  run_loop.Run();
}

TEST_F(TestSmbFsBootstrapImpl, SkipConnect) {
  mojom::SmbFsBootstrapPtr boostrap_ptr;
  SmbFsBootstrapImpl boostrap_impl(mojo::MakeRequest(&boostrap_ptr),
                                   &mock_delegate_);

  EXPECT_CALL(mock_delegate_, CreateSmbFilesystem(kSharePath, _))
      .WillOnce(WithArg<1>([](std::unique_ptr<SmbCredential> credential) {
        std::unique_ptr<MockSmbFilesystem> fs =
            std::make_unique<MockSmbFilesystem>();
        // Expect that EnsureConnected() is never called when skip_connect mount
        // option is set.
        EXPECT_CALL(*fs, EnsureConnected()).Times(0);
        return fs;
      }));
  std::unique_ptr<Filesystem> captured_fs;
  EXPECT_CALL(mock_delegate_, StartFuseSession(_))
      .WillOnce([&captured_fs](std::unique_ptr<Filesystem> fs) {
        // Capture the filesystem to avoid tearing down the SmbFs Mojo service.
        captured_fs = std::move(fs);
        return true;
      });

  mojom::MountOptionsPtr mount_options = mojom::MountOptions::New();
  mount_options->share_path = kSharePath;
  mount_options->skip_connect = true;

  base::RunLoop run_loop;
  boostrap_ptr->MountShare(
      std::move(mount_options), std::move(smbfs_delegate_ptr_),
      base::BindLambdaForTesting([&run_loop](mojom::MountError mount_error,
                                             mojom::SmbFsPtr smbfs_ptr) {
        EXPECT_EQ(mojom::MountError::kOk, mount_error);
        EXPECT_TRUE(smbfs_ptr);
        run_loop.Quit();
      }));
  run_loop.Run();
}

TEST_F(TestSmbFsBootstrapImpl, Disconnect) {
  mojom::SmbFsBootstrapPtr boostrap_ptr;
  SmbFsBootstrapImpl boostrap_impl(mojo::MakeRequest(&boostrap_ptr),
                                   &mock_delegate_);

  base::RunLoop run_loop;
  EXPECT_CALL(mock_delegate_, OnBootstrapConnectionError())
      .WillOnce([&run_loop]() { run_loop.Quit(); });

  boostrap_ptr.reset();
  run_loop.Run();
}

TEST_F(TestSmbFsBootstrapImpl, InvalidPath) {
  mojom::SmbFsBootstrapPtr boostrap_ptr;
  SmbFsBootstrapImpl boostrap_impl(mojo::MakeRequest(&boostrap_ptr),
                                   &mock_delegate_);

  EXPECT_CALL(mock_delegate_, CreateSmbFilesystem(_, _)).Times(0);
  EXPECT_CALL(mock_delegate_, StartFuseSession(_)).Times(0);

  mojom::MountOptionsPtr mount_options = mojom::MountOptions::New();
  mount_options->share_path = "bad-path";

  base::RunLoop run_loop;
  boostrap_ptr->MountShare(
      std::move(mount_options), std::move(smbfs_delegate_ptr_),
      base::BindLambdaForTesting([&run_loop](mojom::MountError mount_error,
                                             mojom::SmbFsPtr smbfs_ptr) {
        EXPECT_EQ(mojom::MountError::kInvalidUrl, mount_error);
        EXPECT_FALSE(smbfs_ptr);
        run_loop.Quit();
      }));
  run_loop.Run();
}

TEST_F(TestSmbFsBootstrapImpl, KerberosSetupFailure) {
  mojom::SmbFsBootstrapPtr boostrap_ptr;
  SmbFsBootstrapImpl boostrap_impl(mojo::MakeRequest(&boostrap_ptr),
                                   &mock_delegate_);

  EXPECT_CALL(mock_delegate_, SetupKerberos(_, _))
      .WillOnce([](mojom::KerberosConfigPtr config,
                   base::OnceCallback<void(bool success)> callback) {
        std::move(callback).Run(false);
      });
  EXPECT_CALL(mock_delegate_, CreateSmbFilesystem(kSharePath, _)).Times(0);
  EXPECT_CALL(mock_delegate_, StartFuseSession(_)).Times(0);

  mojom::MountOptionsPtr mount_options = mojom::MountOptions::New();
  mount_options->share_path = kSharePath;
  mount_options->workgroup = kWorkgroup;
  mount_options->username = kUsername;
  mount_options->kerberos_config = mojom::KerberosConfig::New(
      mojom::KerberosConfig::Source::kKerberos, kKerberosGuid);

  base::RunLoop run_loop;
  boostrap_ptr->MountShare(
      std::move(mount_options), std::move(smbfs_delegate_ptr_),
      base::BindLambdaForTesting([&run_loop](mojom::MountError mount_error,
                                             mojom::SmbFsPtr smbfs_ptr) {
        EXPECT_EQ(mojom::MountError::kUnknown, mount_error);
        EXPECT_FALSE(smbfs_ptr);
        run_loop.Quit();
      }));
  run_loop.Run();
}

TEST_F(TestSmbFsBootstrapImpl, ConnectionAuthFailure) {
  mojom::SmbFsBootstrapPtr boostrap_ptr;
  SmbFsBootstrapImpl boostrap_impl(mojo::MakeRequest(&boostrap_ptr),
                                   &mock_delegate_);

  EXPECT_CALL(mock_delegate_, CreateSmbFilesystem(kSharePath, _))
      .WillOnce(WithArg<1>([](std::unique_ptr<SmbCredential> credential) {
        std::unique_ptr<MockSmbFilesystem> fs =
            std::make_unique<MockSmbFilesystem>();
        EXPECT_CALL(*fs, EnsureConnected())
            .WillOnce(Return(SmbFilesystem::ConnectError::kAccessDenied));
        return fs;
      }));
  EXPECT_CALL(mock_delegate_, StartFuseSession(_)).Times(0);

  mojom::MountOptionsPtr mount_options = mojom::MountOptions::New();
  mount_options->share_path = kSharePath;

  base::RunLoop run_loop;
  boostrap_ptr->MountShare(
      std::move(mount_options), std::move(smbfs_delegate_ptr_),
      base::BindLambdaForTesting([&run_loop](mojom::MountError mount_error,
                                             mojom::SmbFsPtr smbfs_ptr) {
        EXPECT_EQ(mojom::MountError::kAccessDenied, mount_error);
        EXPECT_FALSE(smbfs_ptr);
        run_loop.Quit();
      }));
  run_loop.Run();
}

TEST_F(TestSmbFsBootstrapImpl, UnsupportedProtocolSmb1) {
  mojom::SmbFsBootstrapPtr boostrap_ptr;
  SmbFsBootstrapImpl boostrap_impl(mojo::MakeRequest(&boostrap_ptr),
                                   &mock_delegate_);

  EXPECT_CALL(mock_delegate_, CreateSmbFilesystem(kSharePath, _))
      .WillOnce(WithArg<1>([](std::unique_ptr<SmbCredential> credential) {
        std::unique_ptr<MockSmbFilesystem> fs =
            std::make_unique<MockSmbFilesystem>();
        EXPECT_CALL(*fs, EnsureConnected())
            .WillOnce(Return(SmbFilesystem::ConnectError::kSmb1Unsupported));
        return fs;
      }));
  EXPECT_CALL(mock_delegate_, StartFuseSession(_)).Times(0);

  mojom::MountOptionsPtr mount_options = mojom::MountOptions::New();
  mount_options->share_path = kSharePath;

  base::RunLoop run_loop;
  boostrap_ptr->MountShare(
      std::move(mount_options), std::move(smbfs_delegate_ptr_),
      base::BindLambdaForTesting([&run_loop](mojom::MountError mount_error,
                                             mojom::SmbFsPtr smbfs_ptr) {
        EXPECT_EQ(mojom::MountError::kInvalidProtocol, mount_error);
        EXPECT_FALSE(smbfs_ptr);
        run_loop.Quit();
      }));
  run_loop.Run();
}

TEST_F(TestSmbFsBootstrapImpl, FuseStartFailure) {
  mojom::SmbFsBootstrapPtr boostrap_ptr;
  SmbFsBootstrapImpl boostrap_impl(mojo::MakeRequest(&boostrap_ptr),
                                   &mock_delegate_);

  EXPECT_CALL(mock_delegate_, CreateSmbFilesystem(kSharePath, _))
      .WillOnce(WithArg<1>([](std::unique_ptr<SmbCredential> credential) {
        std::unique_ptr<MockSmbFilesystem> fs =
            std::make_unique<MockSmbFilesystem>();
        EXPECT_CALL(*fs, EnsureConnected())
            .WillOnce(Return(SmbFilesystem::ConnectError::kOk));
        return fs;
      }));
  std::unique_ptr<Filesystem> captured_fs;
  EXPECT_CALL(mock_delegate_, StartFuseSession(_))
      .WillOnce([&captured_fs](std::unique_ptr<Filesystem> fs) {
        // Capture the filesystem to avoid tearing down the SmbFs Mojo service.
        captured_fs = std::move(fs);
        return false;
      });

  mojom::MountOptionsPtr mount_options = mojom::MountOptions::New();
  mount_options->share_path = kSharePath;

  base::RunLoop run_loop;
  boostrap_ptr->MountShare(
      std::move(mount_options), std::move(smbfs_delegate_ptr_),
      base::BindLambdaForTesting([&run_loop](mojom::MountError mount_error,
                                             mojom::SmbFsPtr smbfs_ptr) {
        EXPECT_EQ(mojom::MountError::kUnknown, mount_error);
        EXPECT_FALSE(smbfs_ptr);
        run_loop.Quit();
      }));
  run_loop.Run();
}

}  // namespace
}  // namespace smbfs