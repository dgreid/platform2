// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/vm_collector.h"

#include <string>

#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <google/protobuf/text_format.h>
#include <vm_protos/proto_bindings/vm_crash.grpc.pb.h>

#include "crash-reporter/constants.h"

VmCollector::VmCollector()
    : CrashCollector(
          "vm_collector", kAlwaysUseUserCrashDirectory, kNormalCrashSendMode) {}

bool VmCollector::Collect(pid_t pid) {
  vm_tools::cicerone::CrashReport crash_report;
  google::protobuf::io::FileInputStream input(0 /* stdin */);
  if (!google::protobuf::TextFormat::Parse(&input, &crash_report)) {
    LOG(ERROR) << "Failed to parse crash report from stdin";
    return false;
  }

  base::FilePath crash_path;
  if (!GetCreatedCrashDirectoryByEuid(geteuid(), &crash_path, nullptr,
                                      /*use_non_chronos_cryptohome=*/true)) {
    LOG(ERROR) << "Failed to create or find crash directory";
    return false;
  }
  std::string basename = FormatDumpBasename("vm_crash", time(nullptr), pid);

  base::FilePath meta_path = GetCrashPath(crash_path, basename, "meta");
  base::FilePath proc_log_path = GetCrashPath(crash_path, basename, "proclog");
  base::FilePath minidump_path =
      GetCrashPath(crash_path, basename, constants::kMinidumpExtension);

  int bytes = crash_report.process_tree().size();
  if (WriteNewFile(proc_log_path, crash_report.process_tree().data(), bytes) <
      bytes) {
    LOG(ERROR) << "Failed to write out process tree";
    return false;
  }
  AddCrashMetaUploadFile("process_tree", proc_log_path.BaseName().value());

  bytes = crash_report.minidump().size();
  if (WriteNewFile(minidump_path, crash_report.minidump().data(), bytes) <
      bytes) {
    LOG(ERROR) << "Failed to write out minidump";
    return false;
  }
  AddCrashMetaData("payload", minidump_path.BaseName().value());

  for (const auto& pair : crash_report.metadata()) {
    AddCrashMetaData(pair.first, pair.second);
  }

  // We don't need the data collection code in CrashCollector::FinishCrash (that
  // was already done inside the VM), so just write out the metadata file
  // ourselves.
  WriteNewFile(meta_path, extra_metadata_.data(), extra_metadata_.size());
  return true;
}
