// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/fingerprint_manager.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <brillo/dbus/dbus_object.h>
#include <chromeos/dbus/service_constants.h>

namespace cryptohome {

namespace {

struct AuthScanDBusResult {
  uint32_t scan_result;
  std::vector<std::string> user_ids;
};

// Parses OnAuthScanDone signal from biod. Returns true if parsing succeeds.
bool ParseDBusSignal(dbus::Signal* signal, AuthScanDBusResult* result) {
  dbus::MessageReader signal_reader(signal);

  if (!signal_reader.PopUint32(&result->scan_result))
    return false;

  // Parsing is completed if the scan result isn't success.
  if (result->scan_result != biod::ScanResult::SCAN_RESULT_SUCCESS)
    return true;

  dbus::MessageReader matches_reader(nullptr);
  if (!signal_reader.PopArray(&matches_reader))
    return false;

  while (matches_reader.HasMoreData()) {
    dbus::MessageReader entry_reader(nullptr);
    if (!matches_reader.PopDictEntry(&entry_reader))
      return false;

    std::string user_id;
    if (!entry_reader.PopString(&user_id))
      return false;

    result->user_ids.emplace_back(user_id);
  }

  return true;
}

}  // namespace

std::unique_ptr<FingerprintManager> FingerprintManager::Create(
    const scoped_refptr<dbus::Bus>& bus, const dbus::ObjectPath& path) {
  auto fingerprint_manager = std::make_unique<FingerprintManager>();
  if (!fingerprint_manager->Initialize(bus, path))
    return nullptr;
  return fingerprint_manager;
}

FingerprintManager::FingerprintManager()
    : proxy_(nullptr),
      connected_to_auth_scan_done_signal_(false),
      weak_factory_(this),
      mount_thread_id_(base::PlatformThread::CurrentId()) {}

FingerprintManager::~FingerprintManager() = default;

bool FingerprintManager::Initialize(const scoped_refptr<dbus::Bus>& bus,
                                    const dbus::ObjectPath& path) {
  default_proxy_ = biod::BiometricsManagerProxyBase::Create(bus, path);
  if (!default_proxy_)
    return false;
  default_proxy_->ConnectToAuthScanDoneSignal(
      base::Bind(&FingerprintManager::OnAuthScanDone,
                 weak_factory_.GetWeakPtr()),
      base::Bind(&FingerprintManager::OnAuthScanDoneSignalConnected,
                 weak_factory_.GetWeakPtr()));
  if (!proxy_)
    proxy_ = default_proxy_.get();
  return true;
}

void FingerprintManager::OnAuthScanDoneSignalConnected(
    const std::string& interface, const std::string& signal, bool success) {
  if (!success) {
    LOG(ERROR) << "Failed to connect to signal " << signal << " on interface "
               << interface;
  }
  // If we fail to connect to the AuthScanDone signal, it makes no sense to do
  // subsequent operations.
  connected_to_auth_scan_done_signal_ = success;
}

void FingerprintManager::Reset() {
  current_user_.clear();
  auth_scan_done_callback_.Reset();
}

const std::string& FingerprintManager::GetCurrentUser() {
  return current_user_;
}

base::WeakPtr<FingerprintManager> FingerprintManager::GetWeakPtr() {
  return weak_factory_.GetWeakPtr();
}

void FingerprintManager::OnAuthScanDone(dbus::Signal* signal) {
  VLOG(1) << "Received AuthScanDone signal.";

  // This method is called if any auth scan operation completes, so we validate
  // that this operation is for this class's current user.
  if (current_user_.empty())
    return;

  AuthScanDoneResourceManager resource_manager(proxy_, this);

  AuthScanDBusResult result;
  if (!ParseDBusSignal(signal, &result)) {
    if (auth_scan_done_callback_) {
      auth_scan_done_callback_.Run(
          FingerprintScanStatus::FAILED_RETRY_NOT_ALLOWED);
    }
    return;
  }

  if (result.scan_result != biod::ScanResult::SCAN_RESULT_SUCCESS) {
    VLOG(1) << "Authentication failed: scan result: "
            << biod::ScanResultToString(
                   static_cast<biod::ScanResult>(result.scan_result));
    if (auth_scan_done_callback_)
      auth_scan_done_callback_.Run(FingerprintScanStatus::FAILED_RETRY_ALLOWED);
    return;
  }

  if (std::find(result.user_ids.begin(), result.user_ids.end(),
                current_user_) == result.user_ids.end()) {
    VLOG(1) << "Authentication failed: not matched.";
    // TODO(yichengli): Maintain a retry count for the same user.
    if (auth_scan_done_callback_)
      auth_scan_done_callback_.Run(FingerprintScanStatus::FAILED_RETRY_ALLOWED);
    return;
  }

  VLOG(1) << "Authentication succeeded.";
  if (auth_scan_done_callback_)
    auth_scan_done_callback_.Run(FingerprintScanStatus::SUCCESS);
}

void FingerprintManager::SetAuthScanDoneCallback(
    ResultCallback auth_scan_done_callback) {
  DCHECK(base::PlatformThread::CurrentId() == mount_thread_id_);

  if (!connected_to_auth_scan_done_signal_)
    return;

  // Do nothing if another session is pending.
  if (auth_scan_done_callback_)
    return;
  // |auth_scan_done_callback_| must be set after |current_user_| is set by
  // StartAuthSessionAsyncForUser().
  if (current_user_.empty())
    return;
  auth_scan_done_callback_ = std::move(auth_scan_done_callback);
}

void FingerprintManager::SetUserAndRunClientCallback(
    StartSessionCallback auth_session_start_client_callback,
    const std::string& user,
    bool success) {
  // Set |current_user_| to |user| if auth session started successfully.
  if (success) {
    current_user_ = user;
  } else {
    Reset();
  }
  auth_session_start_client_callback.Run(success);
}

void FingerprintManager::StartAuthSessionAsyncForUser(
    const std::string& user,
    StartSessionCallback auth_session_start_client_callback) {
  DCHECK(base::PlatformThread::CurrentId() == mount_thread_id_);

  if (!connected_to_auth_scan_done_signal_)
    return;

  // Disallow starting auth session if another session might be pending.
  if (!current_user_.empty()) {
    auth_session_start_client_callback.Run(false);
    return;
  }

  // Wrapper callback around the client's callback for starting auth session,
  // so that we can set |current_user_| in addition to running the client's
  // callback.
  auto auth_session_start_callback = base::Bind(
      &FingerprintManager::SetUserAndRunClientCallback, base::Unretained(this),
      std::move(auth_session_start_client_callback), user);

  proxy_->StartAuthSessionAsync(std::move(auth_session_start_callback));
}

void FingerprintManager::SetProxy(biod::BiometricsManagerProxyBase* proxy) {
  proxy_ = proxy;
}

}  // namespace cryptohome
