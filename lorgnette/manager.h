// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LORGNETTE_MANAGER_H_
#define LORGNETTE_MANAGER_H_

#include <map>
#include <memory>
#include <string>

#include <base/callback.h>
#include <base/files/scoped_file.h>
#include <base/macros.h>
#include <brillo/variant_dictionary.h>
#include <brillo/errors/error.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST
#include <metrics/metrics_library.h>

#include "lorgnette/dbus_adaptors/org.chromium.lorgnette.Manager.h"
#include "lorgnette/sane_client.h"

namespace brillo {
class Process;

namespace dbus_utils {
class ExportedObjectManager;
}  // namespace dbus_utils

}  // namespace brillo

namespace lorgnette {

class Minijail;
class FirewallManager;

class Manager : public org::chromium::lorgnette::ManagerAdaptor,
                public org::chromium::lorgnette::ManagerInterface {
 public:
  typedef std::map<std::string, std::map<std::string, std::string>> ScannerInfo;

  Manager(base::Callback<void()> activity_callback,
          std::unique_ptr<SaneClient> sane_client);
  virtual ~Manager();

  void RegisterAsync(
      brillo::dbus_utils::ExportedObjectManager* object_manager,
      brillo::dbus_utils::AsyncEventSequencer* sequencer);

  // Implementation of MethodInterface.
  bool ListScanners(brillo::ErrorPtr* error,
                    ScannerInfo* scanner_list) override;
  bool ScanImage(brillo::ErrorPtr* error,
                 const std::string& device_name,
                 const base::ScopedFD& outfd,
                 const brillo::VariantDictionary& scan_properties) override;

 private:
  friend class ManagerTest;

  FRIEND_TEST(ManagerTest, RunScanImageProcessSuccess);
  FRIEND_TEST(ManagerTest, RunScanImageProcessCaptureFailure);
  FRIEND_TEST(ManagerTest, RunScanImageProcessConvertFailure);

  enum BooleanMetric {
    kBooleanMetricFailure = 0,
    kBooleanMetricSuccess = 1,
    kBooleanMetricMax
  };

  static const char kScanConverterPath[];
  static const char kScanImageFormattedDeviceListCmd[];
  static const char kScanImagePath[];
  static const int kTimeoutAfterKillSeconds;
  static const char kMetricScanResult[];
  static const char kMetricConverterResult[];

  // Starts a scan on |device_name|, outputting PNG image data to |out_fd|.
  // Uses the |pipe_fd_input| and |pipe_fd_output| to transport image data
  // from |scan_process| to |convert_process|.  Uses information from
  // |scan_properties| to set the arguments to the |scan_process|.  Runs both
  // |scan_process| and |convert_process|.  Returns true if |pipe_fds| were
  // consumed, false otherwise.
  void RunScanImageProcess(const std::string& device_name,
                           int out_fd,
                           base::ScopedFD* pipe_fd_input,
                           base::ScopedFD* pipe_fd_output,
                           const brillo::VariantDictionary& scan_properties,
                           brillo::Process* scan_process,
                           brillo::Process* convert_process,
                           brillo::ErrorPtr* error);

  // Converts the formatted output of "scanimage" to a map of attribute-data
  // mappings suitable for returning to a caller to the ListScanners DBus
  // method.
  static ScannerInfo ScannerInfoFromString(
      const std::string& scanner_info_string);

  std::unique_ptr<brillo::dbus_utils::DBusObject> dbus_object_;
  base::Callback<void()> activity_callback_;
  std::unique_ptr<MetricsLibraryInterface> metrics_library_;

  // Manages port access for receiving replies from network scanners.
  std::unique_ptr<FirewallManager> firewall_manager_;

  // Manages connection to SANE for listing and connecting to scanners.
  std::unique_ptr<SaneClient> sane_client_;

  DISALLOW_COPY_AND_ASSIGN(Manager);
};

}  // namespace lorgnette

#endif  // LORGNETTE_MANAGER_H_
