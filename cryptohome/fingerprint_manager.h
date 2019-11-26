// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_FINGERPRINT_MANAGER_H_
#define CRYPTOHOME_FINGERPRINT_MANAGER_H_

#include <memory>
#include <string>

#include "biod/dbus/biometrics_manager_proxy_base.h"

namespace cryptohome {

const char kCrosFpBiometricsManagerRelativePath[] = "/CrosFpBiometricsManager";

enum class FingerprintScanStatus {
  SUCCESS = 0,
  FAILED_RETRY_ALLOWED = 1,
  FAILED_RETRY_NOT_ALLOWED = 2,
};

// FingerprintManager talks to Biometrics Daemon for starting/stopping
// fingerprint auth sessions, and receiving fingerprint auth results.
//
// This class is intended to be used only on a single thread / task runner only.
// Response callbacks will also be run on the same thread / task runner.
class FingerprintManager {
 public:
  using StartSessionCallback = base::Callback<void(bool success)>;
  using ResultCallback = base::Callback<void(FingerprintScanStatus status)>;

  // Factory method. Returns nullptr if Biometrics Daemon is not in a good
  // state or if the device does not have fingerprint support.
  static std::unique_ptr<FingerprintManager> Create(
      const scoped_refptr<dbus::Bus>& bus, const dbus::ObjectPath& path);

  FingerprintManager();
  virtual ~FingerprintManager();

  const std::string& GetCurrentUser();

  // Returns a weak pointer to this instance. Used when creating callbacks.
  base::WeakPtr<FingerprintManager> GetWeakPtr();

  // Starts fingerprint auth session asynchronously, and sets the user if auth
  // session started successfully.
  // |auth_session_start_client_callback| will be called with true if auth
  // session started successfully, or called with false otherwise.
  virtual void StartAuthSessionAsyncForUser(
      const std::string& user,
      StartSessionCallback auth_session_start_client_callback);

  // Sets the callback for a fingerprint scan. Must be called after
  // StartAuthSessionAsyncForUser. |auth_scan_done_callback| will be
  // called with the status of a fingerprint match, once biod sends it.
  virtual void SetAuthScanDoneCallback(ResultCallback auth_scan_done_callback);

  // For testing.
  void SetProxy(biod::BiometricsManagerProxyBase* proxy);

 private:
  friend class FingerprintManagerPeer;

  // Class for properly finish processing an AuthScanDone signal.
  class AuthScanDoneResourceManager {
   public:
    AuthScanDoneResourceManager(biod::BiometricsManagerProxyBase* proxy,
                                FingerprintManager* fingerprint_manager)
        : proxy_(proxy), fingerprint_manager_(fingerprint_manager) {}

    ~AuthScanDoneResourceManager() {
      proxy_->EndAuthSession();
      fingerprint_manager_->Reset();
    }

   private:
    biod::BiometricsManagerProxyBase* proxy_;
    FingerprintManager* fingerprint_manager_;
  };

  // Initializes the underlying dbus object proxy for BiometricsDaemon, and
  // connects to relevant dbus signals. Returns false if failing to get the
  // dbus object proxy (e.g. if biod is not in a good state or the device does
  // not have fingerprint support).
  bool Initialize(const scoped_refptr<dbus::Bus>& bus,
                  const dbus::ObjectPath& path);

  // Callback for connecting to biod's AuthScanDoneSignal.
  void OnAuthScanDoneSignalConnected(const std::string& interface,
                                     const std::string& signal,
                                     bool success);

  // Signal handler biod::kBiometricsManagerAuthScanDoneSignal.
  // Parses the auth scan result from biod, compares the matched user to
  // |current_user_|, and calls |auth_scan_done_callback_|.
  void OnAuthScanDone(dbus::Signal* signal);

  // Internal wrapper around the client's callback for starting auth session
  // asynchronously. If auth session starts successfully, set |current_user_|
  // before running the client's callback.
  void SetUserAndRunClientCallback(
      StartSessionCallback auth_session_start_client_callback,
      const std::string& user,
      bool success);

  void Reset();

  // The default BiometricsManagerProxyBase object.
  std::unique_ptr<biod::BiometricsManagerProxyBase> default_proxy_;
  // The actual BiometricsManagerProxyBase object used in this class.
  // Can be overridden for testing.
  biod::BiometricsManagerProxyBase* proxy_;
  bool connected_to_auth_scan_done_signal_;
  ResultCallback auth_scan_done_callback_;
  std::string current_user_;
  base::WeakPtrFactory<FingerprintManager> weak_factory_;
  base::PlatformThreadId mount_thread_id_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_FINGERPRINT_MANAGER_H_
