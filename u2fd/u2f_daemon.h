// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef U2FD_U2F_DAEMON_H_
#define U2FD_U2F_DAEMON_H_

#include <memory>
#include <string>
#include <vector>

#include <attestation/proto_bindings/interface.pb.h>
#include <attestation-client/attestation/dbus-proxies.h>
#include <base/optional.h>
#include <brillo/daemons/dbus_daemon.h>
#include <brillo/dbus/dbus_object.h>
#include <brillo/dbus/dbus_signal.h>
#include <metrics/metrics_library.h>
#include <power_manager-client/power_manager/dbus-proxies.h>
#include <session_manager/dbus-proxies.h>
#include <u2f/proto_bindings/u2f_interface.pb.h>

#include "u2fd/tpm_vendor_cmd.h"
#include "u2fd/u2f_msg_handler.h"
#include "u2fd/u2fhid.h"
#include "u2fd/webauthn_handler.h"

namespace u2f {

enum class U2fMode : uint8_t;

// U2F Daemon; starts/runs the virtual USB HID U2F device, and implements the
// U2F DBus interface.
class U2fDaemon : public brillo::DBusServiceDaemon {
 public:
  U2fDaemon(bool force_u2f,
            bool force_g2f,
            bool g2f_allowlist_data,
            bool legacy_kh_fallback,
            uint32_t vendor_id,
            uint32_t product_id);
  U2fDaemon(const U2fDaemon&) = delete;
  U2fDaemon& operator=(const U2fDaemon&) = delete;

 protected:
  int OnInit() override;

  // Registers the U2F interface.
  void RegisterDBusObjectsAsync(
      brillo::dbus_utils::AsyncEventSequencer* sequencer) override;

 private:
  // Checks if the device policy is available, and if so, starts the U2F
  // service.
  void TryStartU2fHidService(
      const std::string& /* unused dbus signal status */);

  // Starts the U2F service, and creates the virtual USB HID device. Calling
  // after the service is started is a no-op. Returns:
  //   EX_OK on success
  //   EX_CONFIG if the service is disabled (by flags and/or policy)
  //   EX_PROTOCOL if the cr50 version is incompatible or virtual HID device
  //   cannot be initialized EX_IOERR if DBus cannot be initialized
  int StartU2fHidService();

  // Initializes DBus proxies for PowerManager, SessionManager, and Trunks.
  bool InitializeDBusProxies();

  // Helpers to create these members.
  void CreateU2fMsgHandler(bool allow_g2f_attestation,
                           bool include_g2f_allowlisting_data);
  void CreateU2fHid();
  void InitializeWebAuthnHandler();

  // Sends a DBus signal that indicates to Chrome a 'Press Power Button'
  // notification should be displayed.
  void SendWinkSignal();

  // Calls PowerManager to request that power button presses be ignored for a
  // short time.
  void IgnorePowerButtonPress();

  // Returns a certified copy of the G2F certificate from attestationd, or
  // base::nullopt on error. The size of the G2F certificate is variable, and
  // must be specified in |g2f_cert_size|.
  base::Optional<attestation::GetCertifiedNvIndexReply> GetCertifiedG2fCert(
      int g2f_cert_size);

  // U2F Behavior Flags
  const bool force_u2f_;
  const bool force_g2f_;
  const bool g2f_allowlist_data_;
  const bool legacy_kh_fallback_;

  // Virtual USB Device ID
  uint32_t vendor_id_;
  uint32_t product_id_;

  // DBus
  std::unique_ptr<brillo::dbus_utils::DBusObject> dbus_object_;

  // Signal sent by this daemon
  std::weak_ptr<brillo::dbus_utils::DBusSignal<u2f::UserNotification>>
      wink_signal_;

  // Proxies to call other daemons
  u2f::TpmVendorCommandProxy tpm_proxy_;
  dbus::ObjectProxy* attestation_proxy_;  // Not Owned.
  std::unique_ptr<org::chromium::PowerManagerProxy> pm_proxy_;
  std::unique_ptr<org::chromium::SessionManagerInterfaceProxy> sm_proxy_;

  // User state; uses sm_proxy_.
  std::unique_ptr<UserState> user_state_;

  // Virtual USB Device
  std::unique_ptr<u2f::U2fHid> u2fhid_;
  std::unique_ptr<u2f::U2fMessageHandler> u2f_msg_handler_;

  // WebAuthn DBus Interface Implementation
  WebAuthnHandler webauthn_handler_;

  // UMA, used by Virtual USB Device
  MetricsLibrary metrics_library_;
};

}  // namespace u2f

#endif  // U2FD_U2F_DAEMON_H_
