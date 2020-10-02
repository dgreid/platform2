// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRASH_REPORTER_CRASH_SERIALIZER_H_
#define CRASH_REPORTER_CRASH_SERIALIZER_H_

#include <memory>
#include <string>
#include <vector>

#include <base/time/clock.h>
#include <base/files/file_path.h>
#include <base/files/file.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST

#include "crash-reporter/crash_sender_base.h"
#include "crash-reporter/crash_sender_util.h"
#include "crash-reporter/crash_serializer.pb.h"

namespace crash_serializer {

// A helper class for serializing crashes. Its behaviors can be customized by
// the options struct.
class Serializer : public util::SenderBase {
 public:
  struct Options : public SenderBase::Options {
    // If true, fetch coredumps as well.
    bool fetch_coredumps;
  };
  Serializer(std::unique_ptr<base::Clock> clock, const Options& options);

  // Pick crash files to serialize.
  void PickCrashFiles(const base::FilePath& crash_dir,
                      std::vector<util::MetaFile>* to_send);

  // Serialize the given crashes to the out file
  void SerializeCrashes(const std::vector<util::MetaFile>& crash_meta_files);

  // For tests only. Set the serializer to write output to the specified file
  // instead of stdout.
  void set_output_for_testing(const base::FilePath& file) { out_ = file; }

 protected:
  // SenderBase method
  void RecordCrashRemoveReason(CrashRemoveReason reason) override;

 private:
  FRIEND_TEST(CrashSerializerParameterizedTest, TestSerializeCrash);

  // Serialize a single crash into the given outputs.
  // Populates |core_path| iff fetch_cores_ is true and the core file exists.
  // Does NOT read core into memory as it might be quite large.
  // Return true on success or false on failure.
  // Ignores nonexistent files in info.files, but fails if info.payload is
  // missing.
  bool SerializeCrash(const util::CrashDetails& details,
                      crash::CrashInfo* info,
                      std::vector<crash::CrashBlob>* blobs,
                      base::FilePath* core_path);

  base::FilePath out_;

  // True iff we should fetch core dumps.
  const bool fetch_cores_;
};

}  // namespace crash_serializer

#endif  // CRASH_REPORTER_CRASH_SERIALIZER_H_
