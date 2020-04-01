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

#include <base/synchronization/lock.h>

#include "lorgnette/manager.h"
#include "lorgnette/sane_client.h"

namespace lorgnette {

class SaneClientFake : public SaneClient {
 public:
  bool ListDevices(brillo::ErrorPtr* error,
                   Manager::ScannerInfo* info_out) override;
  std::unique_ptr<SaneDevice> ConnectToDevice(
      brillo::ErrorPtr* error, const std::string& device_name) override;

  void SetListDevicesResult(bool value);
  void AddDevice(const std::string& name,
                 const std::string& manufacturer,
                 const std::string& model,
                 const std::string& type);
  void RemoveDevice(const std::string& name);

 private:
  base::Lock lock_;

  bool list_devices_result_;
  Manager::ScannerInfo scanners_;
};

class SaneDeviceFake : public SaneDevice {
 public:
  SaneDeviceFake();
  ~SaneDeviceFake();

  bool SetScanResolution(brillo::ErrorPtr* error, int resolution) override;
  bool SetScanMode(brillo::ErrorPtr* error,
                   const std::string& scan_mode) override;
  bool StartScan(brillo::ErrorPtr* error) override;
  bool ReadScanData(brillo::ErrorPtr* error,
                    uint8_t* buf,
                    size_t count,
                    size_t* read_out) override;

  void SetStartScanResult(bool result);
  void SetReadScanDataResult(bool result);
  void SetScanData(const std::vector<uint8_t>& scan_data);

 private:
  bool start_scan_result_;
  bool read_scan_data_result_;
  bool scan_running_;
  std::vector<uint8_t> scan_data_;
  size_t scan_data_offset_;
};

}  // namespace lorgnette

#endif  // LORGNETTE_SANE_CLIENT_FAKE_H_
