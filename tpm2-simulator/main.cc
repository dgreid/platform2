// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <brillo/streams/file_stream.h>

extern "C" {
#include <tpm2/_TPM_Init_fp.h>
#include <tpm2/ExecCommand_fp.h>
#include <tpm2/GetCommandCodeString_fp.h>
#include <tpm2/Platform.h>
#include <tpm2/tpm_generated.h>
}

int main() {
  // Initialize TPM.
  _plat__Signal_PowerOn();
  _TPM_Init();
  _plat__SetNvAvail();
  brillo::ErrorPtr error;
  // Create pipes.
  mkfifo("/dev/tpm-req", O_CREAT | O_RDWR);
  mkfifo("/dev/tpm-resp", O_CREAT | O_RDWR);
  brillo::StreamPtr request_stream = brillo::FileStream::Open(
      base::FilePath("/dev/tpm-req"), brillo::Stream::AccessMode::READ_WRITE,
      brillo::FileStream::Disposition::CREATE_ALWAYS, &error);
  if (error.get()) {
    PLOG(ERROR) << "TPM simulator: Error opening /dev/tpm-req: "
                << error->GetMessage();
    return -1;
  }
  brillo::StreamPtr response_stream = brillo::FileStream::Open(
      base::FilePath("/dev/tpm-resp"), brillo::Stream::AccessMode::READ_WRITE,
      brillo::FileStream::Disposition::CREATE_ALWAYS, &error);
  if (error.get()) {
    PLOG(ERROR) << "TPM simulator: Error opening /dev/tpm-resp: "
                << error->GetMessage();
    return -1;
  }
  while (true) {
    unsigned char request[MAX_COMMAND_SIZE];
    unsigned int request_size;
    // Read request header.
    request_stream->ReadAllBlocking(request, 10, &error);
    if (error.get()) {
      PLOG(ERROR) << "TPM simulator: Error receiving request header: "
                  << error->GetMessage();
      return -1;
    }
    unsigned char* header = request;
    INT32 header_size = 10;
    TPMI_ST_COMMAND_TAG tag;
    UINT32 command_size;
    TPM_CC command_code;
    // Unmarshal request header to get request size and command code.
    TPM_RC rc = TPMI_ST_COMMAND_TAG_Unmarshal(&tag, &header, &header_size);
    CHECK_EQ(rc, TPM_RC_SUCCESS);
    rc = UINT32_Unmarshal(&command_size, &header, &header_size);
    CHECK_EQ(rc, TPM_RC_SUCCESS);
    rc = TPM_CC_Unmarshal(&command_code, &header, &header_size);
    CHECK_EQ(rc, TPM_RC_SUCCESS);
    request_size = command_size;
    // Read request body.
    if (request_size > 10) {
      request_stream->ReadAllBlocking(request + 10, request_size - 10, &error);
      if (error.get()) {
        PLOG(ERROR) << "TPM simulator: Error receiving request body: "
                    << error->GetMessage();
        return -1;
      }
    }
    // Execute command.
    LOG(INFO) << "TPM simulator: Executing "
              << GetCommandCodeString(command_code);
    unsigned int response_size;
    unsigned char* response;
    ExecuteCommand(request_size, request, &response_size, &response);
    // Write response.
    response_stream->WriteAllBlocking(response, response_size, &error);
    if (error.get()) {
      PLOG(ERROR) << "TPM simulator: Error writing response: "
                  << error->GetMessage();
      return -1;
    }
  }
}
