// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "trunks/tpm_handle.h"

#include <fcntl.h>
#include <unistd.h>

#include <base/callback.h>
#include <base/logging.h>
#include <base/posix/eintr_wrapper.h>

#include "trunks/trunks_metrics.h"

namespace {

const char kTpmDevice[] = "/dev/tpm0";
const uint32_t kTpmBufferSize = 4096;
const int kInvalidFileDescriptor = -1;

}  // namespace

namespace trunks {

TpmHandle::TpmHandle() : fd_(kInvalidFileDescriptor) {}

TpmHandle::~TpmHandle() {
  int result = IGNORE_EINTR(close(fd_));
  if (result == -1) {
    PLOG(ERROR) << "TPM: couldn't close " << kTpmDevice;
  }
  LOG(INFO) << "TPM: " << kTpmDevice << " closed successfully";
}

bool TpmHandle::Init() {
  if (fd_ != kInvalidFileDescriptor) {
    VLOG(1) << "Tpm already initialized.";
    return true;
  }
  fd_ = HANDLE_EINTR(open(kTpmDevice, O_RDWR));
  if (fd_ == kInvalidFileDescriptor) {
    PLOG(ERROR) << "TPM: Error opening tpm0 file descriptor at " << kTpmDevice;
    return false;
  }
  LOG(INFO) << "TPM: " << kTpmDevice << " opened successfully";
  return true;
}

void TpmHandle::SendCommand(const std::string& command,
                            const ResponseCallback& callback) {
  callback.Run(SendCommandAndWait(command));
}

std::string TpmHandle::SendCommandAndWait(const std::string& command) {
  std::string response;
  TPM_RC result = SendCommandInternal(command, &response);
  if (result != TPM_RC_SUCCESS) {
    response = CreateErrorResponse(result);
    // Send the command code and system uptime of the first timeout command
    if (errno == ETIME) {
      static bool has_reported = false;
      if (!has_reported) {
        TrunksMetrics metrics;
        if (metrics.ReportTpmHandleTimeoutCommandAndTime(result, command))
          has_reported = true;
      }
    }
  }
  return response;
}

TPM_RC TpmHandle::SendCommandInternal(const std::string& command,
                                      std::string* response) {
  CHECK_NE(fd_, kInvalidFileDescriptor);
  int result = HANDLE_EINTR(write(fd_, command.data(), command.length()));
  if (result < 0 && errno == EREMOTEIO) {
    // Retry once in case the error is caused by late wakeup from sleep.
    // Repeated error should lead to failure.
    LOG(WARNING) << "TPM: Retrying write after Remote I/O error.";
    result = HANDLE_EINTR(write(fd_, command.data(), command.length()));
  }
  if (result < 0) {
    PLOG(ERROR) << "TPM: Error writing to TPM handle.";
    return TRUNKS_RC_WRITE_ERROR;
  }
  if (static_cast<size_t>(result) != command.length()) {
    LOG(ERROR) << "TPM: Error writing to TPM handle: " << result << " vs "
               << command.length();
    return TRUNKS_RC_WRITE_ERROR;
  }
  char response_buf[kTpmBufferSize];
  result = HANDLE_EINTR(read(fd_, response_buf, kTpmBufferSize));
  if (result < 0) {
    PLOG(ERROR) << "TPM: Error reading from TPM handle.";
    return TRUNKS_RC_READ_ERROR;
  }
  response->assign(response_buf, static_cast<size_t>(result));
  return TPM_RC_SUCCESS;
}

}  // namespace trunks
