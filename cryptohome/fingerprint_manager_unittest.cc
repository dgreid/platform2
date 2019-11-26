// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/fingerprint_manager.h"

#include <utility>
#include <vector>

#include <base/test/bind_test_util.h>
#include <chromeos/dbus/service_constants.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "biod/dbus/mock_biometrics_manager_proxy_base.h"

namespace cryptohome {

// Peer class for testing FingerprintManager.
class FingerprintManagerPeer {
 public:
  explicit FingerprintManagerPeer(FingerprintManager* fingerprint_manager) {
    fingerprint_manager_ = fingerprint_manager;
  }

  // FingerprintManager won't allow any operation unless
  // |connected_to_auth_scan_done_signal_| is true, so set that for testing.
  void SetConnectedToAuthScanDoneSignal(bool success) {
    fingerprint_manager_->connected_to_auth_scan_done_signal_ = success;
  }

  void SignalAuthScanDone(dbus::Signal* signal) {
    fingerprint_manager_->OnAuthScanDone(signal);
  }

 private:
  FingerprintManager* fingerprint_manager_;
};

namespace {
using testing::_;
using testing::NiceMock;
using testing::Return;

constexpr char kUser[] = "user";

class FingerprintManagerTest : public testing::Test {
 public:
  FingerprintManagerTest() {
    fingerprint_manager_ = std::make_unique<FingerprintManager>();
    fingerprint_manager_->SetProxy(&mock_biod_proxy_);

    fingerprint_manager_peer_ =
        std::make_unique<FingerprintManagerPeer>(fingerprint_manager_.get());
    // Mark |connected_to_auth_scan_done_signal_| to true to allow operations.
    fingerprint_manager_peer_->SetConnectedToAuthScanDoneSignal(true);
  }

  std::unique_ptr<FingerprintManager> fingerprint_manager_;
  std::unique_ptr<FingerprintManagerPeer> fingerprint_manager_peer_;
  NiceMock<biod::MockBiometricsManagerProxyBase> mock_biod_proxy_;
  bool status_;
  FingerprintScanStatus scan_status_;
};

TEST_F(FingerprintManagerTest, StartAuthSessionFail) {
  EXPECT_CALL(mock_biod_proxy_, StartAuthSessionAsync(_))
      .WillOnce([](base::Callback<void(bool success)> callback) {
        std::move(callback).Run(false);
      });
  status_ = true;
  fingerprint_manager_->StartAuthSessionAsyncForUser(
      kUser,
      base::BindLambdaForTesting([this](bool success) { status_ = success; }));
  EXPECT_FALSE(status_);
  EXPECT_TRUE(fingerprint_manager_->GetCurrentUser().empty());
}

TEST_F(FingerprintManagerTest, StartAuthSessionSuccess) {
  EXPECT_CALL(mock_biod_proxy_, StartAuthSessionAsync(_))
      .WillOnce([](base::Callback<void(bool success)> callback) {
        std::move(callback).Run(true);
      });
  status_ = false;
  fingerprint_manager_->StartAuthSessionAsyncForUser(
      kUser,
      base::BindLambdaForTesting([this](bool success) { status_ = success; }));
  EXPECT_TRUE(status_);
  EXPECT_EQ(fingerprint_manager_->GetCurrentUser(), kUser);
}

TEST_F(FingerprintManagerTest, StartAuthSessionTwice) {
  // First auth session still exists.
  EXPECT_CALL(mock_biod_proxy_, StartAuthSessionAsync(_))
      .WillOnce([](base::Callback<void(bool success)> callback) {
        std::move(callback).Run(true);
      });
  status_ = false;
  fingerprint_manager_->StartAuthSessionAsyncForUser(
      kUser,
      base::BindLambdaForTesting([this](bool success) { status_ = success; }));
  EXPECT_TRUE(status_);
  EXPECT_EQ(fingerprint_manager_->GetCurrentUser(), kUser);

  // Second time should fail.
  status_ = true;
  fingerprint_manager_->StartAuthSessionAsyncForUser(
      kUser,
      base::BindLambdaForTesting([this](bool success) { status_ = success; }));
  EXPECT_FALSE(status_);
}

TEST_F(FingerprintManagerTest, AuthScanDoneNoScanResult) {
  EXPECT_CALL(mock_biod_proxy_, StartAuthSessionAsync(_))
      .WillOnce([](base::Callback<void(bool success)> callback) {
        std::move(callback).Run(true);
      });
  fingerprint_manager_->StartAuthSessionAsyncForUser(
      kUser,
      base::BindLambdaForTesting([this](bool success) { status_ = success; }));
  EXPECT_CALL(mock_biod_proxy_, EndAuthSession());

  // This signal does not include a ScanResult, so it's invalid.
  dbus::Signal signal(biod::kBiometricsManagerInterface,
                      biod::kBiometricsManagerAuthScanDoneSignal);

  fingerprint_manager_->SetAuthScanDoneCallback(base::BindLambdaForTesting(
      [this](FingerprintScanStatus status) { scan_status_ = status; }));
  scan_status_ = FingerprintScanStatus::SUCCESS;
  fingerprint_manager_peer_->SignalAuthScanDone(&signal);
  EXPECT_EQ(scan_status_, FingerprintScanStatus::FAILED_RETRY_NOT_ALLOWED);
}

TEST_F(FingerprintManagerTest, AuthScanDoneScanResultFailed) {
  EXPECT_CALL(mock_biod_proxy_, StartAuthSessionAsync(_))
      .WillOnce([](base::Callback<void(bool success)> callback) {
        std::move(callback).Run(true);
      });
  fingerprint_manager_->StartAuthSessionAsyncForUser(
      kUser,
      base::BindLambdaForTesting([this](bool success) { status_ = success; }));
  EXPECT_CALL(mock_biod_proxy_, EndAuthSession());

  dbus::Signal signal(biod::kBiometricsManagerInterface,
                      biod::kBiometricsManagerAuthScanDoneSignal);
  dbus::MessageWriter writer(&signal);
  writer.AppendUint32(
      static_cast<uint32_t>(biod::ScanResult::SCAN_RESULT_PARTIAL));

  fingerprint_manager_->SetAuthScanDoneCallback(base::BindLambdaForTesting(
      [this](FingerprintScanStatus status) { scan_status_ = status; }));
  scan_status_ = FingerprintScanStatus::SUCCESS;
  fingerprint_manager_peer_->SignalAuthScanDone(&signal);
  EXPECT_EQ(scan_status_, FingerprintScanStatus::FAILED_RETRY_ALLOWED);
}

TEST_F(FingerprintManagerTest, AuthScanDoneSuccess) {
  EXPECT_CALL(mock_biod_proxy_, StartAuthSessionAsync(_))
      .WillOnce([](base::Callback<void(bool success)> callback) {
        std::move(callback).Run(true);
      });
  fingerprint_manager_->StartAuthSessionAsyncForUser(
      kUser,
      base::BindLambdaForTesting([this](bool success) { status_ = success; }));
  EXPECT_CALL(mock_biod_proxy_, EndAuthSession());

  dbus::Signal signal(biod::kBiometricsManagerInterface,
                      biod::kBiometricsManagerAuthScanDoneSignal);
  dbus::MessageWriter writer(&signal);
  writer.AppendUint32(
      static_cast<uint32_t>(biod::ScanResult::SCAN_RESULT_SUCCESS));
  dbus::MessageWriter matches_writer(nullptr);
  writer.OpenArray("{sao}", &matches_writer);
  dbus::MessageWriter entry_writer(nullptr);
  matches_writer.OpenDictEntry(&entry_writer);
  entry_writer.AppendString(kUser);
  entry_writer.AppendArrayOfObjectPaths(std::vector<dbus::ObjectPath>());
  matches_writer.CloseContainer(&entry_writer);
  writer.CloseContainer(&matches_writer);
  fingerprint_manager_->SetAuthScanDoneCallback(base::BindLambdaForTesting(
      [this](FingerprintScanStatus status) { scan_status_ = status; }));
  scan_status_ = FingerprintScanStatus::FAILED_RETRY_NOT_ALLOWED;
  fingerprint_manager_peer_->SignalAuthScanDone(&signal);
  EXPECT_EQ(scan_status_, FingerprintScanStatus::SUCCESS);
}

}  // namespace
}  // namespace cryptohome
