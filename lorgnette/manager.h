// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LORGNETTE_MANAGER_H_
#define LORGNETTE_MANAGER_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <base/callback.h>
#include <base/files/scoped_file.h>
#include <brillo/variant_dictionary.h>
#include <brillo/errors/error.h>
#include <metrics/metrics_library.h>

#include <sane/sane.h>

#include "lorgnette/dbus_adaptors/org.chromium.lorgnette.Manager.h"
#include "lorgnette/sane_client.h"

namespace brillo {

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
  bool GetScannerCapabilities(brillo::ErrorPtr* error,
                              const std::string& device_name,
                              std::vector<uint8_t>* capabilities) override;
  bool ScanImage(brillo::ErrorPtr* error,
                 const std::string& device_name,
                 const base::ScopedFD& outfd,
                 const brillo::VariantDictionary& scan_properties) override;

 private:
  friend class ManagerTest;

  enum BooleanMetric {
    kBooleanMetricFailure = 0,
    kBooleanMetricSuccess = 1,
    kBooleanMetricMax
  };

  static const char kMetricScanResult[];
  static const char kMetricConverterResult[];

  static bool ExtractScanOptions(
      brillo::ErrorPtr* error,
      const brillo::VariantDictionary& scan_properties,
      uint32_t* resolution_out,
      std::string* mode_out);

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
