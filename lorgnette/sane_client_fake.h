// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LORGNETTE_SANE_CLIENT_FAKE_H_
#define LORGNETTE_SANE_CLIENT_FAKE_H_

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <base/optional.h>
#include <sane/sane.h>

#include "lorgnette/sane_client.h"

namespace lorgnette {

class SaneDeviceFake;

class SaneClientFake : public SaneClient {
 public:
  base::Optional<std::vector<ScannerInfo>> ListDevices(
      brillo::ErrorPtr* error) override {
    return scanners_;
  }

  void SetListDevicesResult(bool value);
  void AddDevice(const std::string& name,
                 const std::string& manufacturer,
                 const std::string& model,
                 const std::string& type);
  void RemoveDevice(const std::string& name);

  void SetDeviceForName(const std::string& device_name,
                        std::unique_ptr<SaneDeviceFake> device);

 protected:
  std::unique_ptr<SaneDevice> ConnectToDeviceInternal(
      brillo::ErrorPtr* error, const std::string& device_name) override;

 private:
  std::map<std::string, std::unique_ptr<SaneDeviceFake>> devices_;
  bool list_devices_result_;
  std::vector<ScannerInfo> scanners_;
};

class SaneDeviceFake : public SaneDevice {
 public:
  SaneDeviceFake();
  ~SaneDeviceFake();

  base::Optional<ValidOptionValues> GetValidOptionValues(
      brillo::ErrorPtr* error) override;

  base::Optional<int> GetScanResolution(brillo::ErrorPtr* error) override {
    return resolution_;
  }

  bool SetScanResolution(brillo::ErrorPtr* error, int resolution) override;
  base::Optional<std::string> GetDocumentSource(
      brillo::ErrorPtr* error) override {
    return source_name_;
  }
  bool SetDocumentSource(brillo::ErrorPtr* error,
                         const std::string& source_name) override;
  bool SetColorMode(brillo::ErrorPtr* error, ColorMode color_mode) override;
  bool SetScanRegion(brillo::ErrorPtr* error,
                     const ScanRegion& region) override;
  SANE_Status StartScan(brillo::ErrorPtr* error) override;
  base::Optional<ScanParameters> GetScanParameters(
      brillo::ErrorPtr* error) override;
  SANE_Status ReadScanData(brillo::ErrorPtr* error,
                           uint8_t* buf,
                           size_t count,
                           size_t* read_out) override;
  bool CancelScan(brillo::ErrorPtr* error) override;

  void SetValidOptionValues(const base::Optional<ValidOptionValues>& values);
  void SetStartScanResult(SANE_Status status);
  void SetScanParameters(const base::Optional<ScanParameters>& params);
  void SetReadScanDataResult(SANE_Status result);
  void SetScanData(const std::vector<std::vector<uint8_t>>& scan_data);

 private:
  int resolution_;
  std::string source_name_;
  base::Optional<ValidOptionValues> values_;
  SANE_Status start_scan_result_;
  SANE_Status read_scan_data_result_;
  bool scan_running_;
  bool cancelled_;
  base::Optional<ScanParameters> params_;
  std::vector<std::vector<uint8_t>> scan_data_;
  size_t current_page_;
  size_t scan_data_offset_;
};

}  // namespace lorgnette

#endif  // LORGNETTE_SANE_CLIENT_FAKE_H_
