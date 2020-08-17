// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LORGNETTE_SANE_CLIENT_IMPL_H_
#define LORGNETTE_SANE_CLIENT_IMPL_H_

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <base/synchronization/lock.h>
#include <brillo/errors/error.h>
#include <lorgnette/proto_bindings/lorgnette_service.pb.h>
#include <sane/sane.h>

#include "lorgnette/sane_client.h"

namespace lorgnette {

using DeviceSet = std::pair<base::Lock, std::unordered_set<std::string>>;

class SaneClientImpl : public SaneClient {
 public:
  static std::unique_ptr<SaneClientImpl> Create();
  ~SaneClientImpl();

  bool ListDevices(brillo::ErrorPtr* error,
                   std::vector<ScannerInfo>* scanners_out) override;
  std::unique_ptr<SaneDevice> ConnectToDevice(
      brillo::ErrorPtr* error, const std::string& device_name) override;

  static bool DeviceListToScannerInfo(const SANE_Device** device_list,
                                      std::vector<ScannerInfo>* scanners_out);

 private:
  SaneClientImpl();

  base::Lock lock_;
  std::shared_ptr<DeviceSet> open_devices_;
};

class SaneDeviceImpl : public SaneDevice {
  friend class SaneClientImpl;

 public:
  ~SaneDeviceImpl();

  bool GetValidOptionValues(brillo::ErrorPtr* error,
                            ValidOptionValues* values) override;

  bool SetScanResolution(brillo::ErrorPtr* error, int resolution) override;
  bool GetDocumentSource(brillo::ErrorPtr* error,
                         DocumentSource* source_out) override;
  bool SetDocumentSource(brillo::ErrorPtr* error,
                         const DocumentSource& source) override;
  bool SetColorMode(brillo::ErrorPtr* error, ColorMode color_mode) override;
  SANE_Status StartScan(brillo::ErrorPtr* error) override;
  bool GetScanParameters(brillo::ErrorPtr* error,
                         ScanParameters* parameters) override;
  bool ReadScanData(brillo::ErrorPtr* error,
                    uint8_t* buf,
                    size_t count,
                    size_t* read_out) override;

 private:
  enum ScanOption {
    kResolution,
    kScanMode,
    kSource,
  };

  class SaneOption {
   public:
    int index;
    SANE_Value_Type type;  // The type that the backend uses for the option.
    union {
      SANE_Int i;
      SANE_Fixed f;
      SANE_String s;
    } value;

    // The buffer backing value.s, if this is a string option.
    std::vector<char> string_data;

    bool SetInt(int i);
    bool SetString(const std::string& s);
  };

  SaneDeviceImpl(SANE_Handle handle,
                 const std::string& name,
                 std::shared_ptr<DeviceSet> open_devices);
  bool LoadOptions(brillo::ErrorPtr* error);
  SANE_Status SetOption(SaneOption* option, bool* should_reload);

  bool GetValidStringOptionValues(brillo::ErrorPtr* error,
                                  ScanOption option,
                                  std::vector<std::string>* values_out);

  bool GetValidIntOptionValues(brillo::ErrorPtr* error,
                               ScanOption option,
                               std::vector<uint32_t>* values_out);

  SANE_Handle handle_;
  std::string name_;
  std::shared_ptr<DeviceSet> open_devices_;
  std::unordered_map<ScanOption, SaneOption> options_;
  bool scan_running_;
  bool reached_eof_;
};

}  // namespace lorgnette

#endif  // LORGNETTE_SANE_CLIENT_IMPL_H_
