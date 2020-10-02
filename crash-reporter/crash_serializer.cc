// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "crash-reporter/crash_serializer.h"

#include <stdio.h>

#include <string>
#include <utility>

#include <base/compiler_specific.h>  // FALLTHROUGH
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/optional.h>
#include <base/strings/string_util.h>
#include <base/threading/platform_thread.h>
#include <base/time/default_clock.h>
#include <base/time/time.h>

#include "crash-reporter/crash_sender_base.h"
#include "crash-reporter/crash_sender_util.h"
#include "crash-reporter/paths.h"
#include "crash-reporter/util.h"

namespace crash_serializer {

namespace {
void AddMetaField(crash::CrashInfo* info,
                  const std::string& key,
                  const std::string& value) {
  if (!base::IsStringUTF8(key)) {
    LOG(ERROR) << "key was not UTF8: " << key;
    return;
  }
  if (!base::IsStringUTF8(value)) {
    LOG(ERROR) << "value for key '" << key << "' was not UTF8";
    return;
  }
  crash::CrashMetadata* meta = info->add_fields();
  meta->set_key(key);
  meta->set_text(value);
}

base::Optional<crash::CrashBlob> MakeBlob(const std::string& name,
                                          const base::FilePath& file) {
  if (!base::IsStringUTF8(name)) {
    LOG(ERROR) << "key was not UTF8: " << name;
    return base::nullopt;
  }
  std::string contents;
  if (!base::ReadFileToString(file, &contents)) {
    return base::nullopt;
  }
  crash::CrashBlob b;
  b.set_key(name);
  b.set_blob(contents);
  b.set_filename(file.BaseName().value());
  return b;
}

}  // namespace

Serializer::Serializer(std::unique_ptr<base::Clock> clock,
                       const Options& options)
    : util::SenderBase(std::move(clock), options),
      out_("/dev/stdout"),
      fetch_cores_(options.fetch_coredumps) {}

// The serializer doesn't remove crashes, so do nothing.
void Serializer::RecordCrashRemoveReason(CrashRemoveReason reason) {}

void Serializer::PickCrashFiles(const base::FilePath& crash_dir,
                                std::vector<util::MetaFile>* to_send) {
  std::vector<base::FilePath> meta_files = util::GetMetaFiles(crash_dir);

  for (const auto& meta_file : meta_files) {
    LOG(INFO) << "Checking metadata: " << meta_file.value();

    std::string reason;
    util::CrashInfo info;
    switch (EvaluateMetaFileMinimal(meta_file, /*allow_old_os_timestamps=*/true,
                                    &reason, &info,
                                    /*processing_file=*/nullptr)) {
      case kRemove:
        FALLTHROUGH;  // Don't remove; rather, ignore the report.
      case kIgnore:
        LOG(INFO) << "Ignoring: " << reason;
        break;
      case kSend:
        to_send->push_back(std::make_pair(meta_file, std::move(info)));
        break;
      default:
        NOTREACHED();
    }
  }
}

void Serializer::SerializeCrashes(
    const std::vector<util::MetaFile>& crash_meta_files) {
  if (crash_meta_files.empty()) {
    return;
  }

  std::string client_id = util::GetClientId();

  base::File lock(AcquireLockFileOrDie());
  int64_t crash_id = -1;
  for (const auto& pair : crash_meta_files) {
    crash_id++;
    const base::FilePath& meta_file = pair.first;
    const util::CrashInfo& info = pair.second;
    LOG(INFO) << "Evaluating crash report: " << meta_file.value();

    base::TimeDelta sleep_time;
    if (!util::GetSleepTime(meta_file, /*max_spread_time=*/base::TimeDelta(),
                            hold_off_time_, &sleep_time)) {
      LOG(WARNING) << "Failed to compute sleep time for " << meta_file.value();
      continue;
    }

    LOG(INFO) << "Scheduled to send in " << sleep_time.InSeconds() << "s";
    lock.Close();  // Don't hold lock during sleep.
    if (!util::IsMock()) {
      base::PlatformThread::Sleep(sleep_time);
    } else if (!sleep_function_.is_null()) {
      sleep_function_.Run(sleep_time);
    }
    lock = AcquireLockFileOrDie();

    // Mark the crash as being processed so that if we crash, we don't try to
    // send the crash again.
    util::ScopedProcessingFile processing(meta_file);

    // User-specific crash reports become inaccessible if the user signs out
    // while sleeping, thus we need to check if the metadata is still
    // accessible.
    if (!base::PathExists(meta_file)) {
      LOG(INFO) << "Metadata is no longer accessible: " << meta_file.value();
      continue;
    }

    const util::CrashDetails details = {
        .meta_file = meta_file,
        .payload_file = info.payload_file,
        .payload_kind = info.payload_kind,
        .client_id = client_id,
        .metadata = info.metadata,
    };

    crash::FetchCrashesResponse resp;
    resp.set_crash_id(crash_id);
    std::vector<crash::CrashBlob> blobs;
    base::FilePath core_path;
    if (!SerializeCrash(details, resp.mutable_crash(), &blobs, &core_path)) {
      LOG(ERROR) << "Failed to serialize " << meta_file.value();
      continue;
    }

    // TODO(mutexlox): Write response to the output file.
  }
}

bool Serializer::SerializeCrash(const util::CrashDetails& details,
                                crash::CrashInfo* info,
                                std::vector<crash::CrashBlob>* blobs,
                                base::FilePath* core_path) {
  util::FullCrash crash = ReadMetaFile(details);

  // Add fields that are present directly in the FullCrash struct
  info->set_exec_name(crash.exec_name);
  AddMetaField(info, "board", crash.board);
  AddMetaField(info, "hwclass", crash.hwclass);
  info->set_prod(crash.prod);
  info->set_ver(crash.ver);
  info->set_sig(crash.sig);
  AddMetaField(info, "sig2", crash.sig);
  AddMetaField(info, "image_type", crash.image_type);
  AddMetaField(info, "boot_mode", crash.boot_mode);
  AddMetaField(info, "error_type", crash.error_type);
  AddMetaField(info, "guid", crash.guid);

  // Add fields from key_vals
  for (const auto& kv : crash.key_vals) {
    const std::string& key = kv.first;
    const std::string& val = kv.second;
    if (key == "in_progress_integration_test") {
      info->set_in_progress_integration_test(val);
    } else if (key == "collector") {
      info->set_collector(val);
    } else {
      AddMetaField(info, key, val);
    }
  }

  // Add payload file
  base::Optional<crash::CrashBlob> payload =
      MakeBlob(crash.payload.first, crash.payload.second);
  if (!payload) {
    return false;
  }
  blobs->push_back(*payload);

  // Add files
  for (const auto& kv : crash.files) {
    base::Optional<crash::CrashBlob> blob = MakeBlob(kv.first, kv.second);
    if (blob) {
      blobs->push_back(*blob);
    }
  }

  if (fetch_cores_) {
    base::FilePath maybe_core = details.meta_file.ReplaceExtension(".core");
    if (base::PathExists(maybe_core)) {
      *core_path = maybe_core;
    }
  }

  return true;
}

}  // namespace crash_serializer
