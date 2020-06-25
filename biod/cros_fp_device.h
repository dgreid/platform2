// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef BIOD_CROS_FP_DEVICE_H_
#define BIOD_CROS_FP_DEVICE_H_

#include <bitset>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_descriptor_watcher_posix.h>
#include <base/files/file_util.h>

#include "biod/biod_metrics.h"
#include "biod/cros_fp_device_interface.h"
#include "biod/ec_command_factory.h"
#include "biod/fp_info_command.h"
#include "biod/fp_mode.h"
#include "biod/uinput_device.h"

namespace biod {

class CrosFpDevice : public CrosFpDeviceInterface {
 public:
  using MkbpCallback = base::Callback<void(const uint32_t event)>;
  // Use the CrosFpDeviceFactory instead of this constructor unless testing.
  CrosFpDevice(const MkbpCallback& mkbp_event,
               BiodMetricsInterface* biod_metrics,
               std::unique_ptr<EcCommandFactoryInterface> ec_command_factory)
      : ec_command_factory_(std::move(ec_command_factory)),
        mkbp_event_(mkbp_event),
        biod_metrics_(biod_metrics) {}

  bool Init();

  // Run a simple command to get the version information from FP MCU and check
  // whether the image type returned is the same as |expected_image|.
  static bool WaitOnEcBoot(const base::ScopedFD& cros_fp_fd,
                           ec_current_image expected_image);

  // Run a simple command to get the version information from FP MCU.
  // The retrieved version is written to |ver|.
  // Returns true if successfully retrieved the version.
  static bool GetVersion(const base::ScopedFD& cros_fp_fd, EcVersion* ver);

  // CrosFpDeviceInterface overrides:
  ~CrosFpDevice() override;

  bool SetFpMode(const FpMode& mode) override;
  bool GetFpMode(FpMode* mode) override;
  bool GetFpStats(int* capture_ms, int* matcher_ms, int* overall_ms) override;
  bool GetDirtyMap(std::bitset<32>* bitmap) override;
  bool SupportsPositiveMatchSecret() override;
  bool GetPositiveMatchSecret(int index, brillo::SecureBlob* secret) override;
  bool GetTemplate(int index, VendorTemplate* out) override;
  bool UploadTemplate(const VendorTemplate& tmpl) override;
  bool SetContext(std::string user_id) override;
  bool ResetContext() override;
  // Initialise the entropy in the SBP. If |reset| is true, the old entropy
  // will be deleted. If |reset| is false, we will only add entropy, and only
  // if no entropy had been added before.
  bool InitEntropy(bool reset) override;

  int MaxTemplateCount() override;
  int TemplateVersion() override;

  EcCmdVersionSupportStatus EcCmdVersionSupported(uint16_t cmd,
                                                  uint32_t ver) override;

  // Kernel device exposing the MCU command interface.
  static constexpr char kCrosFpPath[] = "/dev/cros_fp";

  // Although very rare, we have seen device commands fail due
  // to ETIMEDOUT. For this reason, we attempt certain critical
  // device IO operation twice.
  static constexpr int kMaxIoAttempts = 2;

  static constexpr int kLastTemplate = -1;

 private:
  bool EcDevInit();
  ssize_t ReadVersion(char* buffer, size_t size);
  bool EcProtoInfo(ssize_t* max_read, ssize_t* max_write);
  bool EcReboot(ec_current_image to_image);
  // Run the EC command to generate new entropy in the underlying MCU.
  // |reset| specifies whether we want to merely add entropy (false), or
  // perform a reset, which erases old entropy(true).
  bool AddEntropy(bool reset);
  // Get block id from rollback info.
  bool GetRollBackInfoId(int32_t* block_id);
  bool FpFrame(int index, std::vector<uint8_t>* frame);
  bool FpReadMatchSecret(uint16_t index, brillo::SecureBlob* secret);
  bool UpdateFpInfo();
  bool GetIndexOfLastTemplate(int* index);
  // Run a sequence of EC commands to update the entropy in the
  // MCU. If |reset| is set to true, it will additionally erase the existing
  // entropy too.
  bool UpdateEntropy(bool reset);

  void OnEventReadable();

  base::ScopedFD cros_fd_;
  std::unique_ptr<base::FileDescriptorWatcher::Controller> watcher_;
  ssize_t max_read_size_ = 0;
  ssize_t max_write_size_ = 0;
  std::unique_ptr<FpInfoCommand> info_;

  std::unique_ptr<EcCommandFactoryInterface> ec_command_factory_;
  MkbpCallback mkbp_event_;
  UinputDevice input_device_;

  BiodMetricsInterface* biod_metrics_ = nullptr;  // Not owned.
};

}  // namespace biod

#endif  // BIOD_CROS_FP_DEVICE_H_
