// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "biod/cros_fp_device.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include <algorithm>

#include <base/bind.h>
#include <base/bind_helpers.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/stringprintf.h>
#include <chromeos/ec/cros_ec_dev.h>

#include "biod/ec_command.h"
#include "biod/ec_command_async.h"
#include "biod/fp_context_command_factory.h"
#include "biod/fp_frame_command.h"

namespace {

std::string FourCC(const uint32_t a) {
  return base::StringPrintf(
      "%c%c%c%c", static_cast<char>(a), static_cast<char>(a >> 8),
      static_cast<char>(a >> 16), static_cast<char>(a >> 24));
}

}  // namespace

namespace biod {

constexpr char CrosFpDevice::kCrosFpPath[];

CrosFpDevice::~CrosFpDevice() {
  // Current session is gone, clean-up temporary state in the FP MCU.
  if (cros_fd_.is_valid())
    ResetContext();
}

bool CrosFpDevice::EcProtoInfo(ssize_t* max_read, ssize_t* max_write) {
  /* read max request / response size from the MCU for protocol v3+ */
  EcCommand<EmptyParam, struct ec_response_get_protocol_info> cmd(
      EC_CMD_GET_PROTOCOL_INFO);
  // We retry this command because it is known to occasionally fail
  // with ETIMEDOUT on first attempt.
  if (!cmd.RunWithMultipleAttempts(cros_fd_.get(), kMaxIoAttempts))
    return false;

  *max_read =
      cmd.Resp()->max_response_packet_size - sizeof(struct ec_host_response);
  // TODO(vpalatin): workaround for b/78544921, can be removed if MCU is fixed.
  *max_write =
      cmd.Resp()->max_request_packet_size - sizeof(struct ec_host_request) - 4;
  return true;
}

ssize_t CrosFpDevice::ReadVersion(char* buffer, size_t size) {
  ssize_t ret;
  for (int retry = 0; retry < kMaxIoAttempts; retry++) {
    ret = read(cros_fd_.get(), buffer, size);
    if (ret >= 0) {
      LOG_IF(INFO, retry > 0)
          << "FPMCU read cros_fp device succeeded on attempt " << retry + 1
          << "/" << kMaxIoAttempts << ".";
      return ret;
    }
    if (errno != ETIMEDOUT) {
      PLOG(ERROR) << "FPMCU failed to read cros_fp device on attempt "
                  << retry + 1 << "/" << kMaxIoAttempts
                  << ", retry is not allowed for error";
      return ret;
    }
    PLOG(ERROR) << "FPMCU failed to read cros_fp device on attempt "
                << retry + 1 << "/" << kMaxIoAttempts;
  }

  return ret;
}

bool CrosFpDevice::EcDevInit() {
  // This is a special read (before events are enabled) that can fail due
  // to ETIMEDOUT. This is because the first read with events disabled
  // triggers a get_version request to the FPMCU, which can timeout.
  // TODO(b/131438292): Remove the hardcoded size for the version buffer.
  char version[80];
  ssize_t ret = ReadVersion(version, sizeof(version) - 1);
  if (ret <= 0) {
    LOG(ERROR) << "Failed to read cros_fp device version, read returned " << ret
               << ".";
    return false;
  }
  version[ret] = '\0';
  LOG(INFO) << "cros_fp device version: " << version;
  char* s = strchr(version, '\n');
  if (s)
    *s = '\0';
  if (strcmp(version, CROS_EC_DEV_VERSION)) {
    LOG(ERROR) << "Invalid device version";
    return false;
  }

  if (!EcProtoInfo(&max_read_size_, &max_write_size_)) {
    LOG(ERROR) << "Failed to get cros_fp protocol info.";
    return false;
  }

  unsigned long mask = 1 << EC_MKBP_EVENT_FINGERPRINT;  // NOLINT(runtime/int)
  if (ioctl(cros_fd_.get(), CROS_EC_DEV_IOCEVENTMASK_V2, mask) < 0) {
    LOG(ERROR) << "Fail to request fingerprint events";
    return false;
  }

  return true;
}

void CrosFpDevice::OnEventReadable() {
  struct ec_response_get_next_event evt;
  ssize_t sz = read(cros_fd_.get(), &evt, sizeof(evt));

  // We are interested only in fingerprint events, discard the other ones.
  if (evt.event_type != EC_MKBP_EVENT_FINGERPRINT ||
      sz < sizeof(evt.event_type) + sizeof(evt.data.fp_events))
    return;

  // Properly aligned event value.
  uint32_t events;
  memcpy(&events, &evt.data.fp_events, sizeof(events));
  mkbp_event_.Run(events);
}

bool CrosFpDevice::SetFpMode(const FpMode& mode) {
  EcCommand<struct ec_params_fp_mode, struct ec_response_fp_mode> cmd(
      EC_CMD_FP_MODE, kVersionZero, {.mode = mode.RawVal()});
  bool ret = cmd.Run(cros_fd_.get());
  if (ret) {
    return true;
  }

  // In some cases the EC Command might go through, but the AP suspends
  // before the EC can ACK it. When the AP wakes up, it considers the
  // EC command to have timed out. Since this seems to happen during mode
  // setting, check the mode in case of a failure.
  FpMode cur_mode = GetFpMode();
  if (cur_mode == FpMode(FpMode::Mode::kModeInvalid)) {
    LOG(ERROR) << "Failed to get FP mode to verify mode was set in the MCU.";
    return false;
  }
  if (cur_mode == mode) {
    LOG(WARNING)
        << "EC Command to set mode failed, but mode was set successfully.";
    return true;
  } else {
    LOG(ERROR) << "EC command to set FP mode: " << mode
               << " failed; current FP mode: " << cur_mode;
  }
  return false;
}

FpMode CrosFpDevice::GetFpMode() {
  EcCommand<struct ec_params_fp_mode, struct ec_response_fp_mode> cmd(
      EC_CMD_FP_MODE, kVersionZero,
      {.mode = static_cast<uint32_t>(FP_MODE_DONT_CHANGE)});
  if (!cmd.Run(cros_fd_.get())) {
    LOG(ERROR) << "Failed to get FP mode from MCU.";
    return FpMode(FpMode::Mode::kModeInvalid);
  }

  return FpMode(cmd.Resp()->mode);
}

EcCmdVersionSupportStatus CrosFpDevice::EcCmdVersionSupported(uint16_t cmd_code,
                                                              uint32_t ver) {
  EcCommand<struct ec_params_get_cmd_versions_v1,
            struct ec_response_get_cmd_versions>
      cmd(EC_CMD_GET_CMD_VERSIONS, 1, {.cmd = cmd_code});

  if (!cmd.RunWithMultipleAttempts(cros_fd_.get(), kMaxIoAttempts) &&
      cmd.Result() == kEcCommandUninitializedResult)
    // Running EC_CMD_GET_CMD_VERSIONS itself failed (e.g. due to timeout).
    return EcCmdVersionSupportStatus::UNKNOWN;

  if (cmd.Result() != EC_RES_SUCCESS)
    // Command not found on EC.
    return EcCmdVersionSupportStatus::UNSUPPORTED;

  if ((cmd.Resp()->version_mask & EC_VER_MASK(ver)) == 0)
    // Command found but version not supported.
    return EcCmdVersionSupportStatus::UNSUPPORTED;

  return EcCmdVersionSupportStatus::SUPPORTED;
}

bool CrosFpDevice::SupportsPositiveMatchSecret() {
  switch (EcCmdVersionSupported(EC_CMD_FP_READ_MATCH_SECRET, 0)) {
    case EcCmdVersionSupportStatus::SUPPORTED:
      LOG(INFO) << "Positive match secret is supported.";
      return true;
    case EcCmdVersionSupportStatus::UNSUPPORTED:
      LOG(INFO) << "Positive match secret is not supported.";
      return false;
    case EcCmdVersionSupportStatus::UNKNOWN:
      LOG(WARNING) << "Failed to check support for positive match secret. "
                      "Defaulting to not supporting.";
      return false;
  }
}

bool CrosFpDevice::FpReadMatchSecret(uint16_t index,
                                     brillo::SecureVector* secret) {
  EcCommand<struct ec_params_fp_read_match_secret,
            struct ec_response_fp_read_match_secret>
      cmd(EC_CMD_FP_READ_MATCH_SECRET, 0, {.fgr = index});

  if (!cmd.Run(cros_fd_.get()) &&
      cmd.Result() == kEcCommandUninitializedResult) {
    LOG(ERROR) << "Failed to run EC_CMD_FP_READ_MATCH_SECRET command.";
    return false;
  }
  if (cmd.Result() != EC_RES_SUCCESS) {
    LOG(ERROR) << "Failed to read positive match secret, result: "
               << cmd.Result() << ".";
    return false;
  }
  secret->resize(sizeof(cmd.Resp()->positive_match_secret));
  std::copy(cmd.Resp()->positive_match_secret,
            cmd.Resp()->positive_match_secret +
                sizeof(cmd.Resp()->positive_match_secret),
            secret->begin());
  brillo::SecureMemset(cmd.Resp()->positive_match_secret, 0,
                       sizeof(cmd.Resp()->positive_match_secret));
  return true;
}

bool CrosFpDevice::UpdateFpInfo() {
  info_ = ec_command_factory_->FpInfoCommand();

  if (!info_->Run(cros_fd_.get())) {
    LOG(ERROR) << "Failed to get FP information.";
    return false;
  }

  return true;
}

base::Optional<CrosFpDeviceInterface::FpStats> CrosFpDevice::GetFpStats() {
  EcCommand<EmptyParam, struct ec_response_fp_stats> cmd(EC_CMD_FP_STATS);
  if (!cmd.Run(cros_fd_.get())) {
    return base::nullopt;
  }

  uint8_t inval = cmd.Resp()->timestamps_invalid;
  if (inval & (FPSTATS_CAPTURE_INV | FPSTATS_MATCHING_INV)) {
    return base::nullopt;
  }

  FpStats stats = {
      .capture_ms = cmd.Resp()->capture_time_us / 1000,
      .matcher_ms = cmd.Resp()->matching_time_us / 1000,
      .overall_ms = cmd.Resp()->overall_time_us / 1000,
  };

  return stats;
}

// static
bool CrosFpDevice::WaitOnEcBoot(const base::ScopedFD& cros_fp_fd,
                                ec_current_image expected_image) {
  int tries = 50;
  ec_current_image image = EC_IMAGE_UNKNOWN;

  while (tries) {
    tries--;
    // Check the EC has the right image.
    EcCommand<EmptyParam, struct ec_response_get_version> cmd(
        EC_CMD_GET_VERSION);
    if (!cmd.Run(cros_fp_fd.get())) {
      LOG(ERROR) << "Failed to retrieve cros_fp firmware version.";
      base::PlatformThread::Sleep(base::TimeDelta::FromMilliseconds(500));
      continue;
    }
    image = static_cast<ec_current_image>(cmd.Resp()->current_image);
    if (image == expected_image) {
      LOG(INFO) << "EC image is " << (image == EC_IMAGE_RO ? "RO" : "RW")
                << ".";
      return true;
    }
    base::PlatformThread::Sleep(base::TimeDelta::FromMilliseconds(100));
  }
  LOG(ERROR) << "EC rebooted to incorrect image " << image;
  return false;
}

// static
bool CrosFpDevice::GetVersion(const base::ScopedFD& cros_fp_fd,
                              EcVersion* ver) {
  CHECK(ver);

  EcCommand<EmptyParam, struct ec_response_get_version> cmd(EC_CMD_GET_VERSION);
  if (!cmd.Run(cros_fp_fd.get())) {
    LOG(ERROR) << "Failed to fetch cros_fp firmware version.";
    return false;
  }

  // buffers should already be null terminated -- this is a safeguard
  cmd.Resp()->version_string_ro[sizeof(cmd.Resp()->version_string_ro) - 1] =
      '\0';
  cmd.Resp()->version_string_rw[sizeof(cmd.Resp()->version_string_rw) - 1] =
      '\0';

  ver->ro_version = std::string(cmd.Resp()->version_string_ro);
  ver->rw_version = std::string(cmd.Resp()->version_string_rw);
  ver->current_image = static_cast<ec_current_image>(cmd.Resp()->current_image);
  return true;
}

bool CrosFpDevice::EcReboot(ec_current_image to_image) {
  DCHECK(to_image == EC_IMAGE_RO || to_image == EC_IMAGE_RW);

  EcCommand<EmptyParam, EmptyParam> cmd_reboot(EC_CMD_REBOOT);
  // Don't expect a return code, cros_fp has rebooted.
  cmd_reboot.Run(cros_fd_.get());

  if (!WaitOnEcBoot(cros_fd_, EC_IMAGE_RO)) {
    LOG(ERROR) << "EC did not come back up after reboot.";
    return false;
  }

  if (to_image == EC_IMAGE_RO) {
    // Tell the EC to remain in RO.
    EcCommand<struct ec_params_rwsig_action, EmptyParam> cmd_rwsig(
        EC_CMD_RWSIG_ACTION);
    cmd_rwsig.SetReq({.action = RWSIG_ACTION_ABORT});
    if (!cmd_rwsig.Run(cros_fd_.get())) {
      LOG(ERROR) << "Failed to keep cros_fp in RO.";
      return false;
    }
  }

  // EC jumps to RW after 1 second. Wait enough time in case we want to reboot
  // to RW. In case we wanted to remain in RO, wait anyway to ensure that the EC
  // received the instructions.
  base::PlatformThread::Sleep(base::TimeDelta::FromSeconds(3));

  if (!WaitOnEcBoot(cros_fd_, to_image)) {
    LOG(ERROR) << "EC did not load the right image.";
    return false;
  }

  return true;
}

bool CrosFpDevice::AddEntropy(bool reset) {
  // Create the secret.
  EcCommandAsync<struct ec_params_rollback_add_entropy, EmptyParam>
      cmd_add_entropy(EC_CMD_ADD_ENTROPY, ADD_ENTROPY_GET_RESULT,
                      {.poll_for_result_num_attempts = 20,
                       .poll_interval = base::TimeDelta::FromMilliseconds(100),
                       // The EC temporarily stops responding to EC commands
                       // when this command is run, so we will keep trying until
                       // we get success (or time out).
                       .validate_poll_result = false});
  if (reset) {
    cmd_add_entropy.SetReq({.action = ADD_ENTROPY_RESET_ASYNC});
  } else {
    cmd_add_entropy.SetReq({.action = ADD_ENTROPY_ASYNC});
  }

  if (cmd_add_entropy.Run(cros_fd_.get())) {
    LOG(INFO) << "Entropy has been successfully added.";
    return true;
  }
  LOG(ERROR) << "Failed to check status of entropy command.";
  return false;
}

bool CrosFpDevice::GetRollBackInfoId(int32_t* block_id) {
  EcCommand<EmptyParam, struct ec_response_rollback_info> cmd_rb_info(
      EC_CMD_ROLLBACK_INFO);
  if (!cmd_rb_info.Run(cros_fd_.get())) {
    return false;
  }

  *block_id = cmd_rb_info.Resp()->id;
  return true;
}

bool CrosFpDevice::InitEntropy(bool reset) {
  int32_t block_id;
  if (!GetRollBackInfoId(&block_id)) {
    LOG(ERROR) << "Failed to read block ID from FPMCU.";
    return false;
  }

  if (!reset && block_id != 0) {
    // Secret has been set.
    LOG(INFO) << "Entropy source had been initialized previously.";
    return true;
  }
  LOG(INFO) << "Entropy source has not been initialized yet.";

  bool success = UpdateEntropy(reset);
  if (!success) {
    LOG(INFO) << "Entropy addition failed.";
    return false;
  }
  LOG(INFO) << "Entropy has been successfully added.";
  return true;
}

bool CrosFpDevice::Init() {
  cros_fd_ = base::ScopedFD(open(kCrosFpPath, O_RDWR));
  if (cros_fd_.get() < 0) {
    LOG(ERROR) << "Failed to open " << kCrosFpPath;
    return false;
  }

  if (!EcDevInit())
    return false;

  if (!InitEntropy(false)) {
    return false;
  }

  // Clean MCU memory if anything is remaining from aborted sessions
  ResetContext();

  // Retrieve the sensor information / parameters.
  if (!UpdateFpInfo())
    return false;

  LOG(INFO) << "CROS FP Sensor Info ";
  LOG(INFO) << "  Vendor ID  : " << FourCC(info_->sensor_id()->vendor_id);
  LOG(INFO) << "  Product ID : " << info_->sensor_id()->product_id;
  LOG(INFO) << "  Model ID   : 0x" << std::hex << info_->sensor_id()->model_id;
  LOG(INFO) << "  Version    : " << info_->sensor_id()->version;
  std::string error_flags;
  if ((info_->GetFpSensorErrors() & FpSensorErrors::kNoIrq) !=
      FpSensorErrors::kNone)
    error_flags += "NO_IRQ ";
  if ((info_->GetFpSensorErrors() & FpSensorErrors::kSpiCommunication) !=
      FpSensorErrors::kNone)
    error_flags += "SPI_COMM ";
  if ((info_->GetFpSensorErrors() & FpSensorErrors::kBadHardwareID) !=
      FpSensorErrors::kNone)
    error_flags += "BAD_HWID ";
  if ((info_->GetFpSensorErrors() & FpSensorErrors::kInitializationFailure) !=
      FpSensorErrors::kNone)
    error_flags += "INIT_FAIL";
  LOG(INFO) << "  Errors     : " << error_flags;
  LOG(INFO) << "CROS FP Image Info ";
  // Prints the pixel format in FOURCC format.
  LOG(INFO) << "  Pixel Format     : "
            << FourCC(info_->sensor_image()->pixel_format);
  LOG(INFO) << "  Image Data Size  : " << info_->sensor_image()->frame_size;
  LOG(INFO) << "  Image Dimensions : " << info_->sensor_image()->width << "x"
            << info_->sensor_image()->height << " "
            << info_->sensor_image()->bpp << " bpp";
  LOG(INFO) << "CROS FP Finger Template Info ";
  LOG(INFO) << "  Template data format  : " << info_->template_info()->version;
  LOG(INFO) << "  Template Data Size    : " << info_->template_info()->size;
  LOG(INFO) << "  Max number of fingers : "
            << info_->template_info()->max_templates;

  auto fp_resp = GetFlashProtect();
  if (!fp_resp) {
    LOG(ERROR) << "Unable to read flash protect state";
  } else {
    LOG(INFO) << "Flash Protect Flags : 0x" << std::hex << fp_resp->flags
              << "\t: " << FpFlashProtectCommand::ParseFlags(fp_resp->flags);
    LOG(INFO) << "Valid Flags         : 0x" << std::hex << fp_resp->valid_flags
              << "\t: "
              << FpFlashProtectCommand::ParseFlags(fp_resp->valid_flags);
    LOG(INFO) << "writable flags      : 0x" << std::hex
              << fp_resp->writable_flags << "\t: "
              << FpFlashProtectCommand::ParseFlags(fp_resp->writable_flags);
  }

  watcher_ = base::FileDescriptorWatcher::WatchReadable(
      cros_fd_.get(), base::BindRepeating(&CrosFpDevice::OnEventReadable,
                                          base::Unretained(this)));
  if (!watcher_) {
    LOG(ERROR) << "Unable to watch MKBP events";
    return false;
  }

  if (!input_device_.Init()) {
    LOG(ERROR) << "Failed to create Uinput device";
    return false;
  }

  return true;
}

bool CrosFpDevice::GetDirtyMap(std::bitset<32>* bitmap) {
  // Retrieve the up-to-date dirty bitmap from the MCU.
  if (!UpdateFpInfo())
    return false;

  *bitmap = info_->template_info()->dirty;
  return true;
}

bool CrosFpDevice::GetIndexOfLastTemplate(int* index) {
  if (!UpdateFpInfo())
    return false;
  *index = info_->template_info()->num_valid - 1;
  if (*index < 0 || *index >= MaxTemplateCount()) {
    LOG(ERROR) << "Invalid index of last template: " << *index << ".";
    return false;
  }
  return true;
}

bool CrosFpDevice::GetPositiveMatchSecret(int index,
                                          brillo::SecureVector* secret) {
  if (index == kLastTemplate) {
    if (!GetIndexOfLastTemplate(&index))
      return false;
  }
  return FpReadMatchSecret(static_cast<uint16_t>(index), secret);
}

bool CrosFpDevice::GetTemplate(int index, VendorTemplate* out) {
  if (index == kLastTemplate) {
    if (!GetIndexOfLastTemplate(&index))
      return false;
    // Is the last one really a new created one ?
    const auto& dirty = info_->template_info()->dirty;
    if (index >= dirty.size() || !dirty.test(index))
      return false;
  }

  // In the EC_CMD_FP_FRAME host command, the templates are indexed starting
  // from 1 (aka FP_FRAME_INDEX_TEMPLATE), as 0 (aka FP_FRAME_INDEX_RAW_IMAGE)
  // is used for the finger image.
  auto fp_frame_cmd = ec_command_factory_->FpFrameCommand(
      index + FP_FRAME_INDEX_TEMPLATE, info_->template_info()->size,
      max_read_size_);
  if (!fp_frame_cmd->Run(cros_fd_.get())) {
    LOG(ERROR) << "Failed to get frame, result: " << fp_frame_cmd->Result();
    return false;
  }
  *out = VendorTemplate(fp_frame_cmd->frame().begin(),
                        fp_frame_cmd->frame().end());
  return true;
}

bool CrosFpDevice::UploadTemplate(const VendorTemplate& tmpl) {
  union cmd_with_data {
    struct ec_params_fp_template req;
    uint8_t _fullsize[kMaxPacketSize];
  };
  EcCommand<union cmd_with_data, EmptyParam> cmd(EC_CMD_FP_TEMPLATE);
  struct ec_params_fp_template* req = &cmd.Req()->req;

  size_t max_chunk =
      max_write_size_ - offsetof(struct ec_params_fp_template, data);

  auto pos = tmpl.begin();
  while (pos < tmpl.end()) {
    size_t remaining = tmpl.end() - pos;
    uint32_t tlen = std::min(max_chunk, remaining);
    req->offset = pos - tmpl.begin();
    req->size = tlen | (remaining == tlen ? FP_TEMPLATE_COMMIT : 0);
    std::copy(pos, pos + tlen, req->data);
    cmd.SetReqSize(tlen + sizeof(struct ec_params_fp_template));
    if (!cmd.Run(cros_fd_.get())) {
      LOG(ERROR) << "Failed to run FP_TEMPLATE command";
      biod_metrics_->SendUploadTemplateResult(metrics::kCmdRunFailure);
      return false;
    }
    if (cmd.Result() != EC_RES_SUCCESS) {
      LOG(ERROR) << "FP_TEMPLATE command failed @ " << pos - tmpl.begin();
      biod_metrics_->SendUploadTemplateResult(cmd.Result());
      return false;
    }
    pos += tlen;
  }
  biod_metrics_->SendUploadTemplateResult(EC_RES_SUCCESS);
  return true;
}

std::unique_ptr<struct ec_response_flash_protect>
CrosFpDevice::GetFlashProtect() {
  auto fp_cmd = ec_command_factory_->FpFlashProtectCommand(0, 0);

  if (!fp_cmd) {
    LOG(ERROR) << "Unable to create FP flash protect command";
    return nullptr;
  }

  bool success = fp_cmd->Run(cros_fd_.get());
  if (!success) {
    return nullptr;
  }

  auto ret = std::make_unique<struct ec_response_flash_protect>();
  memcpy(ret.get(), fp_cmd->Resp(), fp_cmd->RespSize());

  return ret;
}

bool CrosFpDevice::SetContext(std::string user_hex) {
  auto fp_context_cmd = ec_command_factory_->FpContextCommand(this, user_hex);

  if (!fp_context_cmd) {
    LOG(ERROR) << "Unable to create FP context command";
    biod_metrics_->SendSetContextSuccess(false);
    return false;
  }

  bool success = true;
  FpMode original_mode = GetFpMode();
  if (original_mode == FpMode(FpMode::Mode::kModeInvalid)) {
    LOG(ERROR) << "Unable to get FP Mode.";
    success = false;
  }

  // FPMCU does not allow resetting context when mode is not none, to prevent
  // interrupting sensor library and leaking memory. However, for removing
  // fingerprints, since the user is in the fingerprint list UI, FPMCU is in
  // match mode. In this case we have to exit match mode and re-enter after
  // setting context.
  if (original_mode == FpMode(FpMode::Mode::kMatch)) {
    LOG(INFO) << "Attempting to set context with match mode.";
    if (!SetFpMode(FpMode(FpMode::Mode::kNone))) {
      LOG(ERROR) << "Setting FPMCU context: failed to switch mode from match "
                 << "to none.";
      success = false;
    }
  } else if (original_mode != FpMode(FpMode::Mode::kNone)) {
    LOG(ERROR) << "Attempting to set context with mode: " << original_mode
               << ".";
    success = false;
  }
  biod_metrics_->SendSetContextMode(original_mode);

  success &= fp_context_cmd->Run(cros_fd_.get());

  if (original_mode == FpMode(FpMode::Mode::kMatch)) {
    if (!SetFpMode(original_mode)) {
      LOG(ERROR) << "Setting FPMCU context: failed to switch back to match "
                 << "mode after setting context.";
      success = false;
    }
  }

  biod_metrics_->SendSetContextSuccess(success);
  return success;
}

bool CrosFpDevice::ResetContext() {
  FpMode cur_mode = GetFpMode();
  if (cur_mode == FpMode(FpMode::Mode::kModeInvalid)) {
    LOG(ERROR) << "Unable to get FP Mode.";
  }

  // ResetContext is called when we no longer expect any session to be running
  // (such as when the user logs out or biod is starting/stopping). This check
  // exists to make sure that we have disabled any matching in the firmware
  // when this is called. See https://crbug.com/980614 for details.
  if (cur_mode != FpMode(FpMode::Mode::kNone)) {
    LOG(ERROR) << "Attempting to reset context with mode: " << cur_mode;
  }

  CHECK(biod_metrics_);
  biod_metrics_->SendResetContextMode(cur_mode);

  return SetContext(std::string());
}

bool CrosFpDevice::UpdateEntropy(bool reset) {
  // Stash the most recent block id.
  int32_t block_id;
  if (!GetRollBackInfoId(&block_id)) {
    LOG(ERROR) << "Failed to block ID from FPMCU before entropy reset.";
    return false;
  }

  // Reboot the EC to RO.
  if (!EcReboot(EC_IMAGE_RO)) {
    LOG(ERROR) << "Failed to reboot cros_fp to initialise entropy.";
    return false;
  }

  // Initialize the secret.
  if (!AddEntropy(reset)) {
    LOG(ERROR) << "Failed to add entropy.";
    return false;
  }

  // Entropy added, reboot cros_fp to RW.
  if (!EcReboot(EC_IMAGE_RW)) {
    LOG(ERROR) << "Failed to reboot cros_fp after initializing entropy.";
    return false;
  }

  int32_t new_block_id;
  if (!GetRollBackInfoId(&new_block_id)) {
    LOG(ERROR) << "Failed to block ID from FPMCU after entropy reset.";
    return false;
  }

  int32_t block_id_diff = 2;
  if (!reset) {
    block_id_diff = 1;
  }

  if (new_block_id != block_id + block_id_diff) {
    LOG(ERROR) << "Entropy source has not been updated; old block_id: "
               << block_id << ", new block_id: " << new_block_id;
    return false;
  }
  return true;
}

int CrosFpDevice::MaxTemplateCount() {
  if (!info_ || !info_->template_info()) {
    UpdateFpInfo();
  }
  CHECK(info_);
  CHECK(info_->template_info());
  return info_->template_info()->max_templates;
}

int CrosFpDevice::TemplateVersion() {
  if (!info_ || !info_->template_info()) {
    UpdateFpInfo();
  }
  CHECK(info_);
  CHECK(info_->template_info());
  return info_->template_info()->version;
}

int CrosFpDevice::DeadPixelCount() {
  if (!info_ || !info_->template_info()) {
    UpdateFpInfo();
  }
  CHECK(info_);
  CHECK(info_->template_info());
  return info_->NumDeadPixels();
}

void CrosFpDevice::SetMkbpEventCallback(CrosFpDevice::MkbpCallback callback) {
  mkbp_event_ = callback;
}

}  // namespace biod
