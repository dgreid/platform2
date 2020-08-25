// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "debugd/src/verify_ro_tool.h"

#include <unistd.h>

#include <memory>
#include <string>
#include <utility>

#include <base/files/file_path.h>
#include <base/files/file_util.h>

#include "debugd/src/error_utils.h"
#include "debugd/src/process_with_id.h"

namespace {

constexpr char kCr50VerifyRoScript[] = "/usr/share/cros/cr50-verify-ro.sh";

// Parent dir of where Cr50 image and RO db files are stored.
constexpr char kCr50ResourcePath[] = "/opt/google/cr50/";

// The user and group kCr50VerifyRoScript is run as in the sandbox.
constexpr char kFwCheckerAndUpdater[] = "rma_fw_keeper";
constexpr char kSuzyqAccessGroup[] = "suzy-q";

constexpr char kVerifyRoToolErrorString[] =
    "org.chromium.debugd.error.VerifyRo";

}  // namespace

namespace debugd {

bool VerifyRoTool::UpdateAndVerifyFWOnUsb(brillo::ErrorPtr* error,
                                          const base::ScopedFD& outfd,
                                          const std::string& image_file,
                                          const std::string& ro_db_dir,
                                          std::string* handle) {
  if (!CheckCr50ResourceLocation(image_file, false /* is_dir */)) {
    DEBUGD_ADD_ERROR(error, kVerifyRoToolErrorString,
                     "Bad FW image file: " + image_file);
    return false;
  }

  if (!CheckCr50ResourceLocation(ro_db_dir, true /* is_dir */)) {
    DEBUGD_ADD_ERROR(error, kVerifyRoToolErrorString,
                     "Bad ro descriptor dir: " + ro_db_dir);
    return false;
  }

  auto p = std::make_unique<ProcessWithId>();

  p->SandboxAs(kFwCheckerAndUpdater, kSuzyqAccessGroup);
  if (!p->Init()) {
    DEBUGD_ADD_ERROR(error, kVerifyRoToolErrorString,
                     "Could not initialize the verify_ro process.");
    return false;
  }

  p->AddArg(kCr50VerifyRoScript);
  p->AddArg(image_file);
  p->AddArg(ro_db_dir);

  p->BindFd(outfd.get(), STDOUT_FILENO);
  p->BindFd(outfd.get(), STDERR_FILENO);

  if (!p->Start()) {
    DEBUGD_ADD_ERROR(error, kVerifyRoToolErrorString,
                     "Failed to run the verify_ro process.");
    return false;
  }

  *handle = p->id();

  if (!RecordProcess(std::move(p))) {
    DEBUGD_ADD_ERROR(error, kVerifyRoToolErrorString,
                     "Failed to record the verify_ro process.");
    return false;
  }

  return true;
}

bool VerifyRoTool::CheckCr50ResourceLocation(const std::string& path,
                                             bool is_dir) {
  base::FilePath absolute_path =
      base::MakeAbsoluteFilePath(base::FilePath(path));
  if (absolute_path.empty()) {
    // |path| doesn't exist.
    return false;
  }

  if (is_dir && !base::DirectoryExists(absolute_path)) {
    // |path| is not a dir.
    return false;
  }

  // Using absolute path here to avoid path spoofing, e.g.,
  // /opt/google/cr50/../../../tmp/badfile
  return base::StartsWith(absolute_path.value(), kCr50ResourcePath,
                          base::CompareCase::SENSITIVE);
}

}  // namespace debugd
