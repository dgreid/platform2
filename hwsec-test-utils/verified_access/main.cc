// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hwsec-test-utils/verified_access/verified_access.h"

#include <stdio.h>

#include <base/base64.h>
#include <base/command_line.h>
#include <base/optional.h>
#include <brillo/data_encoding.h>
#include <brillo/syslog_logging.h>

namespace {

constexpr char kGenerateCommand[] = "generate";
const char kUsage[] = R"(
Usage: hwsec-test-va <command> [<args>]
Commands:
  |generate|
      Generates a VA challenge signed with well-known VA signing key and prints
      the base64-encoded result in stdout.
)";

constexpr char kExpectedChallengePrefix[] = "EnterpriseKeyChallenge";

void PrintUsage() {
  printf("%s", kUsage);
}

}  // namespace

int main(int argc, char* argv[]) {
  base::CommandLine::Init(argc, argv);
  brillo::InitLog(brillo::kLogToStderr);

  base::CommandLine* cl = base::CommandLine::ForCurrentProcess();
  const auto& args = cl->GetArgs();
  if (args.empty()) {
    PrintUsage();
    return 1;
  }
  if (args.front() == kGenerateCommand) {
    hwsec_test_utils::verified_access::VerifiedAccessChallenge va;
    base::Optional<attestation::SignedData> challenge =
        va.GenerateChallenge(kExpectedChallengePrefix);
    if (!challenge) {
      printf("Failed to generate VA challenge.\n");
      return 1;
    }
    std::string serialized_challenge;
    if (!challenge->SerializeToString(&serialized_challenge)) {
      printf("Failed to serialize VA challenge.\n");
      return 1;
    }
    printf("%s",
           brillo::data_encoding::Base64Encode(serialized_challenge).c_str());
    return 0;
  }
  PrintUsage();
  return 1;
}
