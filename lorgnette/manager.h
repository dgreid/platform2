// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LORGNETTE_MANAGER_H_
#define LORGNETTE_MANAGER_H_

#include <stdint.h>

#include <memory>
#include <string>
#include <vector>

#include <base/callback.h>
#include <base/callback_helpers.h>
#include <base/containers/flat_map.h>
#include <base/containers/flat_set.h>
#include <base/files/scoped_file.h>
#include <base/memory/weak_ptr.h>
#include <base/optional.h>
#include <base/synchronization/lock.h>
#include <base/time/time.h>
#include <brillo/errors/error.h>
#include <lorgnette/proto_bindings/lorgnette_service.pb.h>
#include <metrics/metrics_library.h>

#include "lorgnette/dbus_adaptors/org.chromium.lorgnette.Manager.h"
#include "lorgnette/sane_client.h"

using brillo::dbus_utils::DBusMethodResponse;

namespace brillo {

namespace dbus_utils {
class ExportedObjectManager;
}  // namespace dbus_utils

}  // namespace brillo

namespace lorgnette {

namespace impl {

// Returns a byte vector containing the serialized representation of |proto|.
template <typename T>
std::vector<uint8_t> SerializeProto(const T& proto) {
  std::vector<uint8_t> serialized;
  serialized.resize(proto.ByteSizeLong());
  proto.SerializeToArray(serialized.data(), serialized.size());
  return serialized;
}

// Attempts to parse a ColorMode from the mode names used by SANE. If |mode|
// is not recognized, returns MODE_UNSPECIFIED.
ColorMode ColorModeFromSaneString(const std::string& mode);

}  // namespace impl

using StatusSignalSender =
    base::RepeatingCallback<void(const ScanStatusChangedSignal&)>;

class FirewallManager;

class Manager : public org::chromium::lorgnette::ManagerAdaptor,
                public org::chromium::lorgnette::ManagerInterface {
 public:
  Manager(base::Callback<void()> activity_callback,
          std::unique_ptr<SaneClient> sane_client);
  Manager(const Manager&) = delete;
  Manager& operator=(const Manager&) = delete;
  virtual ~Manager();

  void RegisterAsync(brillo::dbus_utils::ExportedObjectManager* object_manager,
                     brillo::dbus_utils::AsyncEventSequencer* sequencer);

  // Implementation of MethodInterface.
  bool ListScanners(brillo::ErrorPtr* error,
                    std::vector<uint8_t>* scanner_list_out) override;
  bool GetScannerCapabilities(brillo::ErrorPtr* error,
                              const std::string& device_name,
                              std::vector<uint8_t>* capabilities) override;
  std::vector<uint8_t> StartScan(
      const std::vector<uint8_t>& start_scan_request) override;
  void GetNextImage(
      std::unique_ptr<DBusMethodResponse<std::vector<uint8_t>>> response,
      const std::vector<uint8_t>& get_next_image_request,
      const base::ScopedFD& out_fd) override;
  std::vector<uint8_t> CancelScan(
      const std::vector<uint8_t>& cancel_scan_request) override;

  void SetProgressSignalInterval(base::TimeDelta interval);

  // Register the callback to call when we send a ScanStatusChanged signal for
  // tests.
  void SetScanStatusChangedSignalSenderForTest(StatusSignalSender sender);

  void RemoveDuplicateScanners(std::vector<ScannerInfo>* scanners,
                               base::flat_set<std::string> seen_vidpid,
                               base::flat_set<std::string> seen_busdev,
                               const std::vector<ScannerInfo>& sane_scanners);

 private:
  friend class ManagerTest;

  struct ScanJobState {
    std::string device_name;
    bool in_use = false;
    std::unique_ptr<SaneDevice> device;
    int current_page = 1;
    // The total number of pages to scan for the scan job. If this is nullopt,
    // keep scanning until we get an error.
    base::Optional<int> total_pages;
  };

  static const char kMetricScanRequested[];
  static const char kMetricScanSucceeded[];
  static const char kMetricScanFailed[];

  bool StartScanInternal(brillo::ErrorPtr* error,
                         const StartScanRequest& request,
                         std::unique_ptr<SaneDevice>* device_out);

  void GetNextImageInternal(const std::string& uuid,
                            ScanJobState* scan_state,
                            base::ScopedFILE out_file);

  ScanState RunScanLoop(brillo::ErrorPtr* error,
                        ScanJobState* scan_state,
                        base::ScopedFILE out_file,
                        base::Optional<std::string> scan_uuid);

  void ReportScanRequested(const std::string& device_name);
  void ReportScanSucceeded(const std::string& device_name);
  void ReportScanFailed(const std::string& device_name);

  void SendStatusSignal(std::string uuid,
                        ScanState state,
                        int page,
                        int progress,
                        bool more_pages);
  void SendCancelledSignal(std::string uuid);
  void SendFailureSignal(std::string uuid, std::string failure_reason);

  std::unique_ptr<brillo::dbus_utils::DBusObject> dbus_object_;
  base::Callback<void()> activity_callback_;
  std::unique_ptr<MetricsLibraryInterface> metrics_library_;

  // Manages port access for receiving replies from network scanners.
  std::unique_ptr<FirewallManager> firewall_manager_;

  // Manages connection to SANE for listing and connecting to scanners.
  std::unique_ptr<SaneClient> sane_client_;

  // A callback to call when we attempt to send a D-Bus signal. This is used
  // for testing in order to track the signals sent from StartScan.
  StatusSignalSender status_signal_sender_;
  base::TimeDelta progress_signal_interval_;

  base::Lock active_scans_lock_;
  // Mapping from scan UUIDs to the state for that scan job.
  base::flat_map<std::string, ScanJobState> active_scans_;

  // Keep as the last member variable.
  base::WeakPtrFactory<Manager> weak_factory_{this};
};

}  // namespace lorgnette

#endif  // LORGNETTE_MANAGER_H_
