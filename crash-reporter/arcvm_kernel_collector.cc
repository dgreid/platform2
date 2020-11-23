// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/arcvm_kernel_collector.h"

#include <sstream>
#include <utility>

#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/strings/stringprintf.h>
#include <brillo/syslog_logging.h>

#include "crash-reporter/arc_util.h"
#include "crash-reporter/constants.h"
#include "crash-reporter/util.h"

namespace {

constexpr char kArcvmKernelCollectorName[] = "ARCVM_kernel";
constexpr char kArcvmKernelCrashType[] =
    "arcvm_kernel_crash";  // This is not a crash_type of Android.

// This value comes from the size of a ring buffer in the backend file of
// pstore (/home/root/<hash>/crosvm/*.pstore). The size of the ring buffer
// depends on the implementation of Linux kernel for pstore and the current
// kernel parameters of ARCVM kernel (go/arcvm-pstore-dump).
constexpr size_t kRamoopsMaxFileSize = 0x5f000 - 12;

constexpr char kKernelExecName[] = "arcvm-kernel";
constexpr pid_t kKernelPid = 0;
constexpr char kRamoopsExtension[] = "log";

}  // namespace

ArcvmKernelCollector::ArcvmKernelCollector()
    : CrashCollector(kArcvmKernelCollectorName,
                     kAlwaysUseUserCrashDirectory,
                     kNormalCrashSendMode) {}

ArcvmKernelCollector::~ArcvmKernelCollector() = default;

bool ArcvmKernelCollector::HandleCrash(
    const arc_util::BuildProperty& build_property) {
  return HandleCrashWithRamoopsStreamAndTimestamp(build_property, stdin,
                                                  time(nullptr));
}

bool ArcvmKernelCollector::HandleCrashWithRamoopsStreamAndTimestamp(
    const arc_util::BuildProperty& build_property,
    FILE* ramoops_stream,
    time_t timestamp) {
  LogCrash("Received crash notification for ARCVM kernel", "handling");

  bool out_of_capacity = false;
  base::FilePath crash_dir;
  if (!GetCreatedCrashDirectoryByEuid(geteuid(), &crash_dir,
                                      &out_of_capacity)) {
    LOG(ERROR) << "Failed to create or find crash directory";
    if (!out_of_capacity)
      EnqueueCollectionErrorLog(kErrorSystemIssue, kKernelExecName);
    return false;
  }

  AddArcMetadata(build_property);

  std::string ramoops_content;
  if (!base::ReadStreamToStringWithMaxSize(ramoops_stream, kRamoopsMaxFileSize,
                                           &ramoops_content)) {
    LOG(ERROR) << "Failed to read rammoops from stdin";
    return false;
  }
  StripSensitiveData(&ramoops_content);
  const std::string basename_without_ext =
      FormatDumpBasename(kKernelExecName, timestamp, kKernelPid);
  const base::FilePath ramoops_path =
      GetCrashPath(crash_dir, basename_without_ext, kRamoopsExtension);
  if (!WriteNewFile(ramoops_path, ramoops_content.c_str(),
                    ramoops_content.size())) {
    LOG(ERROR) << "Failed to write ramoops to file: " << ramoops_path;
    return false;
  }
  // TODO(kimiyuki): Compute the signature of the crash from |ramoops_content|.

  const base::FilePath metadata_path =
      GetCrashPath(crash_dir, basename_without_ext, "meta");
  FinishCrash(metadata_path, kKernelExecName, ramoops_path.BaseName().value());

  return true;
}

void ArcvmKernelCollector::AddArcMetadata(
    const arc_util::BuildProperty& build_property) {
  AddCrashMetaUploadData(arc_util::kProductField, arc_util::kArcProduct);
  AddCrashMetaUploadData(arc_util::kProcessField, kKernelExecName);
  AddCrashMetaUploadData(arc_util::kCrashTypeField, kArcvmKernelCrashType);
  AddCrashMetaUploadData(arc_util::kChromeOsVersionField,
                         CrashCollector::GetOsVersion());
  for (const auto& metadata :
       arc_util::ListMetadataForBuildProperty(build_property)) {
    AddCrashMetaUploadData(metadata.first, metadata.second);
  }
}