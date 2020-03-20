/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef HARDWARE_VERIFIER_CLI_H_
#define HARDWARE_VERIFIER_CLI_H_

#include <memory>
#include <ostream>
#include <string>

#include <base/macros.h>

#include "hardware_verifier/hw_verification_spec_getter.h"
#include "hardware_verifier/probe_result_getter.h"
#include "hardware_verifier/verifier.h"

namespace hardware_verifier {

enum CLIVerificationResult {
  kPass = 0,

  // The whole process works without errors, but the verification
  // report shows the device is not compliant.
  kFail,

  // Failed to load the probe result from the specific file.
  kInvalidProbeResultFile,

  // Failed to load the verification payload from either the default one
  // or the specific one.
  kInvalidHwVerificationSpecFile,

  // The |runtime_probe| fails to return a valid probe result.
  kProbeFail,

  // Content in the verification payload and the probe result are not matched
  // to each other.
  kProbeResultHwVerificationSpecMisalignment,

  kUnknownError
};

enum CLIOutputFormat {
  kProtoBin,  // Protobuf binary format.
  kText       // Human readable text format for debug purpose.
};

// A class that holds the core logic of the program if runs in CLI mode.
class CLI {
 public:
  // Constructor, it sets the dependent classes to the default implementation.
  CLI();

  // Verifies the probe result with the verification payload and then outputs
  // the report.
  //
  // @param probe_result_file: Path to the file that contains the probe result.
  //     If the string is empty, it invokes |runtime_probe| to get the probe
  //     result.
  // @param hw_verification_spec_file: Path to the file that contains the
  //     verification payload.  If the string is empty, it reads the default
  //     verification payload file in the rootfs.
  // @param output_format: The format of the output data.
  //
  // @return Execution result, can be either the verification result or the
  //     failure code.
  CLIVerificationResult Run(const std::string& probe_result_file,
                            const std::string& hw_verification_spec_file,
                            const CLIOutputFormat output_format);

 private:
  friend class CLITest;

  // Dependent classes.
  std::unique_ptr<ProbeResultGetter> pr_getter_;
  std::unique_ptr<HwVerificationSpecGetter> vp_getter_;
  std::unique_ptr<Verifier> verifier_;

  // Instance to the output stream, default to |std::cout|.
  std::ostream* output_stream_;

  DISALLOW_COPY_AND_ASSIGN(CLI);
};

}  // namespace hardware_verifier

#endif  // HARDWARE_VERIFIER_CLI_H_
