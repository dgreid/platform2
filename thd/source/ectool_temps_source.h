// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THD_SOURCE_ECTOOL_TEMPS_SOURCE_H_
#define THD_SOURCE_ECTOOL_TEMPS_SOURCE_H_

#include "thd/source/source.h"

#include <cstdint>
#include <string>
#include <vector>

#include <base/macros.h>

namespace thd {

// Source to read temperatures through the "ectools temps x" command
//
// Note on usage:
// This source is for testing, debugging, and creating proofs of concept. It's
// prone to breaking easily since it just parses the command line output from
// ectool.
// Should any of the temperatures exposed through this source become actually
// necessary for a production configuration of sysfs, please build a proper
// exposure mechanism (like a /sys/class/thermal/ zone) for it.
class EctoolTempsSource : public Source {
 public:
  // |sensor_id| informs what sensors ID to read when calling "ectool temps x".
  explicit EctoolTempsSource(int sensor_id);
  EctoolTempsSource(const EctoolTempsSource&) = delete;
  EctoolTempsSource& operator=(const EctoolTempsSource&) = delete;

  ~EctoolTempsSource() override;

  // Source:
  bool ReadValue(int64_t* value_out) override;

 private:
  // The arguments to use for the command line are stored here on construction,
  // and used to call "ectool temps x" on each ReadValue call.
  const std::vector<std::string> cmd_args_;
};

}  // namespace thd

#endif  // THD_SOURCE_ECTOOL_TEMPS_SOURCE_H_
