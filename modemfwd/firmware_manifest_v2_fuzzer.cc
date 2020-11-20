// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstddef>
#include <cstdint>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <libprotobuf-mutator/src/libfuzzer/libfuzzer_macro.h>

#include "modemfwd/firmware_manifest.h"
#include "modemfwd/proto_bindings/firmware_manifest_v2.pb.h"

namespace modemfwd {

class Environment {
 public:
  Environment() {
    logging::SetMinLogLevel(logging::LOGGING_FATAL);  // Disable logging.
  }
};

void FuzzParseFirmwareManifestV2(const modemfwd::FirmwareManifestV2& input) {
  static Environment env;

  std::string text;
  google::protobuf::TextFormat::PrintToString(input, &text);

  base::ScopedTempDir temp_dir_;
  CHECK(temp_dir_.CreateUniqueTempDir());
  const char kManifestName[] = "firmware_manifest.prototxt";
  base::FilePath file_path = temp_dir_.GetPath().Append(kManifestName);

  base::WriteFile(file_path, text.data(), text.size());

  FirmwareIndex index;

  ParseFirmwareManifestV2(file_path, &index);
}

DEFINE_PROTO_FUZZER(const modemfwd::FirmwareManifestV2& input) {
  FuzzParseFirmwareManifestV2(input);
}

}  // namespace modemfwd
