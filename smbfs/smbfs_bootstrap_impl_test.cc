// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "smbfs/smbfs_bootstrap_impl.h"

#include <memory>
#include <utility>
#include <vector>

#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/files/scoped_temp_dir.h>
#include <base/run_loop.h>
#include <base/test/bind_test_util.h>
#include <base/test/task_environment.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <mojo/core/embedder/embedder.h>
#include <mojo/public/cpp/system/platform_handle.h>
#include <libpasswordprovider/password.h>

#include "smbfs/smb_filesystem.h"
#include "smbfs/smbfs_impl.h"

namespace smbfs {
namespace {

using ::testing::_;
using ::testing::Return;
using ::testing::Unused;
using ::testing::WithArg;

const char kSharePath[] = "smb://server/share";
const char kWorkgroup[] = "my-workgroup";
const char kUsername[] = "my-username";
const char kPassword[] = "my-super-secret-password";
const char kKerberosGuid[] = "1234-5678-my-guid";
const char kAccountHash[] = "00112233445566778899aa";

class MockSmbFilesystemDelegate : public SmbFilesystem::Delegate {
 public:
  MOCK_METHOD(void,
              RequestCredentials,
              (RequestCredentialsCallback),
              (override));
};

class MockSmbFilesystem : public SmbFilesystem {
 public:
  MockSmbFilesystem() : SmbFilesystem(&delegate_, kSharePath) {}

  MOCK_METHOD(ConnectError, EnsureConnected, (), (override));
  MOCK_METHOD(void,
              SetResolvedAddress,
              (const std::vector<uint8_t>&),
              (override));

 private:
  MockSmbFilesystemDelegate delegate_;
};

class MockBootstrapDelegate : public SmbFsBootstrapImpl::Delegate {
 public:
  MOCK_METHOD(void,
              SetupKerberos,
              (mojom::KerberosConfigPtr,
               base::OnceCallback<void(bool success)>),
              (override));
  MOCK_METHOD(void, OnPasswordFilePathSet, (const base::FilePath&), (override));
};

class MockSmbFsDelegate : public mojom::SmbFsDelegate {
 public:
  explicit MockSmbFsDelegate(mojom::SmbFsDelegateRequest request)
      : binding_(this, std::move(request)) {}

  MOCK_METHOD(void,
              RequestCredentials,
              (RequestCredentialsCallback),
              (override));

 private:
  mojo::Binding<mojom::SmbFsDelegate> binding_;
};

std::unique_ptr<password_provider::Password> MakePassword(
    const std::string& password) {
  int fds[2];
  CHECK(base::CreateLocalNonBlockingPipe(fds));
  base::ScopedFD read_fd(fds[0]);
  base::ScopedFD write_fd(fds[1]);
  CHECK(base::WriteFileDescriptor(write_fd.get(), password.data(),
                                  password.size()));
  return password_provider::Password::CreateFromFileDescriptor(read_fd.get(),
                                                               password.size());
}

class TestSmbFsBootstrapImpl : public testing::Test {
 public:
  void SetUp() override {
    ResetDelegate();

    ASSERT_TRUE(daemon_store_dir_.CreateUniqueTempDir());
  }

  void ResetDelegate() {
    smbfs_delegate_ptr_.reset();
    mock_smbfs_delegate_ = std::make_unique<MockSmbFsDelegate>(
        mojo::MakeRequest(&smbfs_delegate_ptr_));
  }

 protected:
  base::test::TaskEnvironment task_environment{
      base::test::TaskEnvironment::ThreadingMode::MAIN_THREAD_ONLY,
      base::test::TaskEnvironment::MainThreadType::IO};
  MockBootstrapDelegate mock_delegate_;

  mojom::SmbFsDelegatePtr smbfs_delegate_ptr_;
  std::unique_ptr<MockSmbFsDelegate> mock_smbfs_delegate_;

  base::ScopedTempDir daemon_store_dir_;
};

TEST_F(TestSmbFsBootstrapImpl, GuestAuth) {
  mojom::SmbFsBootstrapPtr boostrap_ptr;

  auto fs_factory = base::BindLambdaForTesting(
      [](SmbFilesystem::Options options) -> std::unique_ptr<SmbFilesystem> {
        EXPECT_EQ(options.share_path, kSharePath);
        EXPECT_FALSE(options.allow_ntlm);
        EXPECT_TRUE(options.credentials->workgroup.empty());
        EXPECT_TRUE(options.credentials->username.empty());
        EXPECT_FALSE(options.credentials->password);

        std::unique_ptr<MockSmbFilesystem> fs =
            std::make_unique<MockSmbFilesystem>();
        EXPECT_CALL(*fs, EnsureConnected())
            .WillOnce(Return(SmbFilesystem::ConnectError::kOk));
        EXPECT_CALL(*fs, SetResolvedAddress(std::vector<uint8_t>({1, 2, 3, 4})))
            .Times(1);
        return fs;
      });
  SmbFsBootstrapImpl boostrap_impl(mojo::MakeRequest(&boostrap_ptr), fs_factory,
                                   &mock_delegate_,
                                   daemon_store_dir_.GetPath());
  bool bootstrap_done = false;
  boostrap_impl.Start(base::BindLambdaForTesting(
      [&bootstrap_done](std::unique_ptr<SmbFilesystem> fs,
                        mojom::SmbFsRequest smbfs_request,
                        mojom::SmbFsDelegatePtr delegate_ptr) {
        EXPECT_TRUE(fs);
        bootstrap_done = true;
      }));

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
  EXPECT_TRUE(bootstrap_done);
}

TEST_F(TestSmbFsBootstrapImpl, UsernamePasswordAuth) {
  mojom::SmbFsBootstrapPtr boostrap_ptr;

  auto fs_factory = base::BindLambdaForTesting(
      [](SmbFilesystem::Options options) -> std::unique_ptr<SmbFilesystem> {
        EXPECT_EQ(options.share_path, kSharePath);
        EXPECT_TRUE(options.allow_ntlm);
        EXPECT_EQ(options.credentials->workgroup, kWorkgroup);
        EXPECT_EQ(options.credentials->username, kUsername);
        EXPECT_EQ(options.credentials->password->GetRaw(),
                  std::string(kPassword));

        std::unique_ptr<MockSmbFilesystem> fs =
            std::make_unique<MockSmbFilesystem>();
        EXPECT_CALL(*fs, EnsureConnected())
            .WillOnce(Return(SmbFilesystem::ConnectError::kOk));
        EXPECT_CALL(*fs, SetResolvedAddress(_)).Times(0);
        return fs;
      });
  SmbFsBootstrapImpl boostrap_impl(mojo::MakeRequest(&boostrap_ptr), fs_factory,
                                   &mock_delegate_,
                                   daemon_store_dir_.GetPath());
  bool bootstrap_done = false;
  boostrap_impl.Start(base::BindLambdaForTesting(
      [&bootstrap_done](std::unique_ptr<SmbFilesystem> fs,
                        mojom::SmbFsRequest smbfs_request,
                        mojom::SmbFsDelegatePtr delegate_ptr) {
        EXPECT_TRUE(fs);
        bootstrap_done = true;
      }));

  mojom::MountOptionsPtr mount_options = mojom::MountOptions::New();
  mount_options->share_path = kSharePath;
  mount_options->workgroup = kWorkgroup;
  mount_options->username = kUsername;
  mount_options->password = MakePassword(kPassword);
  mount_options->allow_ntlm = true;

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
  EXPECT_TRUE(bootstrap_done);
}

TEST_F(TestSmbFsBootstrapImpl, KerberosAuth) {
  mojom::SmbFsBootstrapPtr boostrap_ptr;

  auto fs_factory = base::BindLambdaForTesting(
      [](SmbFilesystem::Options options) -> std::unique_ptr<SmbFilesystem> {
        EXPECT_EQ(options.share_path, kSharePath);
        EXPECT_FALSE(options.allow_ntlm);
        EXPECT_EQ(options.credentials->workgroup, kWorkgroup);
        EXPECT_EQ(options.credentials->username, kUsername);
        EXPECT_FALSE(options.credentials->password);

        std::unique_ptr<MockSmbFilesystem> fs =
            std::make_unique<MockSmbFilesystem>();
        EXPECT_CALL(*fs, EnsureConnected())
            .WillOnce(Return(SmbFilesystem::ConnectError::kOk));
        EXPECT_CALL(*fs, SetResolvedAddress(_)).Times(0);
        return fs;
      });
  SmbFsBootstrapImpl boostrap_impl(mojo::MakeRequest(&boostrap_ptr), fs_factory,
                                   &mock_delegate_,
                                   daemon_store_dir_.GetPath());

  EXPECT_CALL(mock_delegate_, SetupKerberos(_, _))
      .WillOnce([](mojom::KerberosConfigPtr config,
                   base::OnceCallback<void(bool success)> callback) {
        EXPECT_EQ(config->source, mojom::KerberosConfig::Source::kKerberos);
        EXPECT_EQ(config->identity, kKerberosGuid);
        std::move(callback).Run(true);
      });
  bool bootstrap_done = false;
  boostrap_impl.Start(base::BindLambdaForTesting(
      [&bootstrap_done](std::unique_ptr<SmbFilesystem> fs,
                        mojom::SmbFsRequest smbfs_request,
                        mojom::SmbFsDelegatePtr delegate_ptr) {
        EXPECT_TRUE(fs);
        bootstrap_done = true;
      }));

  mojom::MountOptionsPtr mount_options = mojom::MountOptions::New();
  mount_options->share_path = kSharePath;
  mount_options->workgroup = kWorkgroup;
  mount_options->username = kUsername;
  mount_options->kerberos_config = mojom::KerberosConfig::New(
      mojom::KerberosConfig::Source::kKerberos, kKerberosGuid);
  // These two options will be ignored when Kerberos is being used.
  mount_options->password = MakePassword(kPassword);
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
  EXPECT_TRUE(bootstrap_done);
}

TEST_F(TestSmbFsBootstrapImpl, SkipConnect) {
  mojom::SmbFsBootstrapPtr boostrap_ptr;

  auto fs_factory = base::BindLambdaForTesting(
      [](SmbFilesystem::Options options) -> std::unique_ptr<SmbFilesystem> {
        EXPECT_EQ(options.share_path, kSharePath);
        EXPECT_FALSE(options.allow_ntlm);

        std::unique_ptr<MockSmbFilesystem> fs =
            std::make_unique<MockSmbFilesystem>();
        // Expect that EnsureConnected() is never called when skip_connect mount
        // option is set.
        EXPECT_CALL(*fs, EnsureConnected()).Times(0);
        return fs;
      });
  SmbFsBootstrapImpl boostrap_impl(mojo::MakeRequest(&boostrap_ptr), fs_factory,
                                   &mock_delegate_,
                                   daemon_store_dir_.GetPath());

  bool bootstrap_done = false;
  boostrap_impl.Start(base::BindLambdaForTesting(
      [&bootstrap_done](std::unique_ptr<SmbFilesystem> fs,
                        mojom::SmbFsRequest smbfs_request,
                        mojom::SmbFsDelegatePtr delegate_ptr) {
        EXPECT_TRUE(fs);
        bootstrap_done = true;
      }));

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
  EXPECT_TRUE(bootstrap_done);
}

TEST_F(TestSmbFsBootstrapImpl, Disconnect) {
  mojom::SmbFsBootstrapPtr boostrap_ptr;

  auto fs_factory = base::BindLambdaForTesting(
      [](SmbFilesystem::Options options) -> std::unique_ptr<SmbFilesystem> {
        // FAIL() can only be used in a function that returns void.
        ADD_FAILURE();
        return nullptr;
      });
  SmbFsBootstrapImpl boostrap_impl(mojo::MakeRequest(&boostrap_ptr), fs_factory,
                                   &mock_delegate_,
                                   daemon_store_dir_.GetPath());

  base::RunLoop run_loop;
  boostrap_impl.Start(base::BindLambdaForTesting(
      [&run_loop](std::unique_ptr<SmbFilesystem> fs,
                  mojom::SmbFsRequest smbfs_request,
                  mojom::SmbFsDelegatePtr delegate_ptr) {
        EXPECT_FALSE(fs);
        run_loop.Quit();
      }));

  boostrap_ptr.reset();
  run_loop.Run();
}

TEST_F(TestSmbFsBootstrapImpl, InvalidPath) {
  mojom::SmbFsBootstrapPtr boostrap_ptr;

  auto fs_factory = base::BindLambdaForTesting(
      [](SmbFilesystem::Options options) -> std::unique_ptr<SmbFilesystem> {
        ADD_FAILURE();
        return nullptr;
      });
  SmbFsBootstrapImpl boostrap_impl(mojo::MakeRequest(&boostrap_ptr), fs_factory,
                                   &mock_delegate_,
                                   daemon_store_dir_.GetPath());

  boostrap_impl.Start(base::BindOnce(
      [](std::unique_ptr<SmbFilesystem> fs, mojom::SmbFsRequest smbfs_request,
         mojom::SmbFsDelegatePtr delegate_ptr) { FAIL(); }));

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

  auto fs_factory = base::BindLambdaForTesting(
      [](SmbFilesystem::Options options) -> std::unique_ptr<SmbFilesystem> {
        ADD_FAILURE();
        return nullptr;
      });
  SmbFsBootstrapImpl boostrap_impl(mojo::MakeRequest(&boostrap_ptr), fs_factory,
                                   &mock_delegate_,
                                   daemon_store_dir_.GetPath());

  EXPECT_CALL(mock_delegate_, SetupKerberos(_, _))
      .WillOnce([](mojom::KerberosConfigPtr config,
                   base::OnceCallback<void(bool success)> callback) {
        std::move(callback).Run(false);
      });
  boostrap_impl.Start(base::BindOnce(
      [](std::unique_ptr<SmbFilesystem> fs, mojom::SmbFsRequest smbfs_request,
         mojom::SmbFsDelegatePtr delegate_ptr) { FAIL(); }));

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

  auto fs_factory = base::BindLambdaForTesting(
      [](SmbFilesystem::Options options) -> std::unique_ptr<SmbFilesystem> {
        EXPECT_EQ(options.share_path, kSharePath);

        std::unique_ptr<MockSmbFilesystem> fs =
            std::make_unique<MockSmbFilesystem>();
        EXPECT_CALL(*fs, EnsureConnected())
            .WillOnce(Return(SmbFilesystem::ConnectError::kAccessDenied));
        return fs;
      });
  SmbFsBootstrapImpl boostrap_impl(mojo::MakeRequest(&boostrap_ptr), fs_factory,
                                   &mock_delegate_,
                                   daemon_store_dir_.GetPath());

  boostrap_impl.Start(base::BindOnce(
      [](std::unique_ptr<SmbFilesystem> fs, mojom::SmbFsRequest smbfs_request,
         mojom::SmbFsDelegatePtr delegate_ptr) { FAIL(); }));

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

  auto fs_factory = base::BindLambdaForTesting(
      [](SmbFilesystem::Options options) -> std::unique_ptr<SmbFilesystem> {
        EXPECT_EQ(options.share_path, kSharePath);

        std::unique_ptr<MockSmbFilesystem> fs =
            std::make_unique<MockSmbFilesystem>();
        EXPECT_CALL(*fs, EnsureConnected())
            .WillOnce(Return(SmbFilesystem::ConnectError::kSmb1Unsupported));
        return fs;
      });
  SmbFsBootstrapImpl boostrap_impl(mojo::MakeRequest(&boostrap_ptr), fs_factory,
                                   &mock_delegate_,
                                   daemon_store_dir_.GetPath());

  boostrap_impl.Start(base::BindOnce(
      [](std::unique_ptr<SmbFilesystem> fs, mojom::SmbFsRequest smbfs_request,
         mojom::SmbFsDelegatePtr delegate_ptr) { FAIL(); }));

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

TEST_F(TestSmbFsBootstrapImpl, SaveRestorePassword) {
  const std::vector<uint8_t> salt(
      mojom::CredentialStorageOptions::kMinSaltLength, 'a');

  const base::FilePath user_directory =
      daemon_store_dir_.GetPath().Append(kAccountHash);
  ASSERT_TRUE(base::CreateDirectory(user_directory));
  EXPECT_TRUE(base::IsDirectoryEmpty(user_directory));

  {
    mojom::SmbFsBootstrapPtr boostrap_ptr;
    auto fs_factory = base::BindLambdaForTesting(
        [](SmbFilesystem::Options options) -> std::unique_ptr<SmbFilesystem> {
          std::unique_ptr<MockSmbFilesystem> fs =
              std::make_unique<MockSmbFilesystem>();
          EXPECT_CALL(*fs, EnsureConnected())
              .WillOnce(Return(SmbFilesystem::ConnectError::kOk));
          return fs;
        });
    EXPECT_CALL(mock_delegate_, OnPasswordFilePathSet(_))
        .WillOnce([user_directory](const base::FilePath& path) {
          EXPECT_TRUE(user_directory.IsParent(path));
        });
    SmbFsBootstrapImpl boostrap_impl(mojo::MakeRequest(&boostrap_ptr),
                                     fs_factory, &mock_delegate_,
                                     daemon_store_dir_.GetPath());
    boostrap_impl.Start(base::BindLambdaForTesting(
        [](std::unique_ptr<SmbFilesystem> fs, mojom::SmbFsRequest smbfs_request,
           mojom::SmbFsDelegatePtr delegate_ptr) { EXPECT_TRUE(fs); }));

    mojom::MountOptionsPtr mount_options = mojom::MountOptions::New();
    mount_options->share_path = kSharePath;
    mount_options->workgroup = kWorkgroup;
    mount_options->username = kUsername;
    mount_options->password = MakePassword(kPassword);
    mount_options->credential_storage_options =
        mojom::CredentialStorageOptions::New(kAccountHash, salt);

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

  // There should be a file in the user's daemon-store directory.
  EXPECT_FALSE(base::IsDirectoryEmpty(user_directory));

  // Since the file's name and contents are obfuscated, don't check them
  // directly. Instead, do another mount operation which uses the saved
  // password.
  {
    mojom::SmbFsBootstrapPtr boostrap_ptr;
    auto fs_factory = base::BindLambdaForTesting(
        [](SmbFilesystem::Options options) -> std::unique_ptr<SmbFilesystem> {
          EXPECT_EQ(options.credentials->workgroup, kWorkgroup);
          EXPECT_EQ(options.credentials->username, kUsername);
          EXPECT_EQ(options.credentials->password->GetRaw(),
                    std::string(kPassword));

          std::unique_ptr<MockSmbFilesystem> fs =
              std::make_unique<MockSmbFilesystem>();
          EXPECT_CALL(*fs, EnsureConnected())
              .WillOnce(Return(SmbFilesystem::ConnectError::kOk));
          return fs;
        });
    EXPECT_CALL(mock_delegate_, OnPasswordFilePathSet(_))
        .WillOnce([user_directory](const base::FilePath& path) {
          EXPECT_TRUE(user_directory.IsParent(path));
        });
    SmbFsBootstrapImpl boostrap_impl(mojo::MakeRequest(&boostrap_ptr),
                                     fs_factory, &mock_delegate_,
                                     daemon_store_dir_.GetPath());
    boostrap_impl.Start(base::BindLambdaForTesting(
        [](std::unique_ptr<SmbFilesystem> fs, mojom::SmbFsRequest smbfs_request,
           mojom::SmbFsDelegatePtr delegate_ptr) { EXPECT_TRUE(fs); }));

    mojom::MountOptionsPtr mount_options = mojom::MountOptions::New();
    mount_options->share_path = kSharePath;
    mount_options->workgroup = kWorkgroup;
    mount_options->username = kUsername;
    // When there's no password field, implicitly restore the password from a
    // file.
    mount_options->credential_storage_options =
        mojom::CredentialStorageOptions::New(kAccountHash, salt);

    base::RunLoop run_loop;
    ResetDelegate();
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
}

TEST_F(TestSmbFsBootstrapImpl, NoSavePasswordOnMountFailure) {
  const std::vector<uint8_t> salt(
      mojom::CredentialStorageOptions::kMinSaltLength, 'a');

  const base::FilePath user_directory =
      daemon_store_dir_.GetPath().Append(kAccountHash);
  ASSERT_TRUE(base::CreateDirectory(user_directory));
  EXPECT_TRUE(base::IsDirectoryEmpty(user_directory));

  {
    mojom::SmbFsBootstrapPtr boostrap_ptr;
    auto fs_factory = base::BindLambdaForTesting(
        [](SmbFilesystem::Options options) -> std::unique_ptr<SmbFilesystem> {
          std::unique_ptr<MockSmbFilesystem> fs =
              std::make_unique<MockSmbFilesystem>();
          EXPECT_CALL(*fs, EnsureConnected())
              .WillOnce(Return(SmbFilesystem::ConnectError::kAccessDenied));
          return fs;
        });
    SmbFsBootstrapImpl boostrap_impl(mojo::MakeRequest(&boostrap_ptr),
                                     fs_factory, &mock_delegate_,
                                     daemon_store_dir_.GetPath());
    boostrap_impl.Start(base::BindLambdaForTesting(
        [](std::unique_ptr<SmbFilesystem> fs, mojom::SmbFsRequest smbfs_request,
           mojom::SmbFsDelegatePtr delegate_ptr) { EXPECT_TRUE(fs); }));

    mojom::MountOptionsPtr mount_options = mojom::MountOptions::New();
    mount_options->share_path = kSharePath;
    mount_options->workgroup = kWorkgroup;
    mount_options->username = kUsername;
    mount_options->password = MakePassword(kPassword);
    mount_options->credential_storage_options =
        mojom::CredentialStorageOptions::New(kAccountHash, salt);

    base::RunLoop run_loop;
    boostrap_ptr->MountShare(
        std::move(mount_options), std::move(smbfs_delegate_ptr_),
        base::BindLambdaForTesting([&run_loop](mojom::MountError mount_error,
                                               mojom::SmbFsPtr smbfs_ptr) {
          EXPECT_EQ(mojom::MountError::kAccessDenied, mount_error);
          run_loop.Quit();
        }));
    run_loop.Run();
  }

  // There should be no files in the user's daemon-store.
  EXPECT_TRUE(base::IsDirectoryEmpty(user_directory));
}

}  // namespace
}  // namespace smbfs
