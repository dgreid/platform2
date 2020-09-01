// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/bind.h>
#include <base/bind_helpers.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/macros.h>
#include <base/strings/stringprintf.h>
#include <base/synchronization/waitable_event.h>
#include <base/task/single_thread_task_executor.h>
#include <brillo/file_utils.h>
#include <brillo/flag_helper.h>
#include <brillo/syslog_logging.h>

#include "sealed_storage/sealed_storage.h"
#include "sealed_storage/wrapper.h"

namespace {

sealed_storage::Policy ConstructPolicy(bool verified_boot_mode,
                                       bool dev_mode,
                                       int32_t unchanged_pcr) {
  sealed_storage::Policy::PcrMap pcr_map;

  auto val_boot_mode = sealed_storage::Policy::BootModePCR(
      verified_boot_mode ? sealed_storage::kVerifiedBootMode
                         : sealed_storage::kDevMode);
  auto val_unchanged_pcr = sealed_storage::Policy::UnchangedPCR(
      unchanged_pcr >= 0 ? unchanged_pcr : 100);
  std::string descr;

  if ((verified_boot_mode || dev_mode) && unchanged_pcr >= 0) {
    pcr_map = {val_boot_mode, val_unchanged_pcr};
    descr = base::StringPrintf("%s, PCR%d",
                               verified_boot_mode ? "verified_boot" : "dev",
                               unchanged_pcr);
  } else if (verified_boot_mode || dev_mode) {
    pcr_map = {val_boot_mode};
    descr = verified_boot_mode ? "verified_boot" : "dev";
  } else if (unchanged_pcr >= 0) {
    pcr_map = {val_unchanged_pcr};
    descr = base::StringPrintf("PCR%d", unchanged_pcr);
  }
  LOG(INFO) << "Policy: {" << descr << "}";
  return {pcr_map};
}

}  // namespace

bool RunTest(const sealed_storage::SealedStorage& storage,
             int32_t extend_pcr,
             bool expected_before,
             bool expected_after) {
  const std::string kTestData = "testTest";

  LOG(INFO) << ">>>> Test START.";
  LOG(INFO) << "Expected initial state: "
            << (expected_before ? "matches policy." : "doesn't match policy.");
  auto matches = storage.CheckState();
  if (!matches.has_value()) {
    LOG(ERROR) << "FAILURE: CheckState failed.";
    return false;
  }
  if (matches.value() != expected_before) {
    LOG(ERROR) << "FAILURE: Unexpected state.";
    return false;
  }

  LOG(INFO) << "Performing: Seal, expected to succeed.";
  sealed_storage::SecretData data(kTestData.begin(), kTestData.end());
  auto blob = storage.Seal(data);
  if (!blob) {
    LOG(ERROR) << "FAILURE: Seal failed.";
    return false;
  }

  if (extend_pcr >= 0) {
    LOG(INFO) << "Performing: Extend PCR" << extend_pcr
              << ", expected to succeed.";
    if (!storage.ExtendPCR(extend_pcr)) {
      LOG(ERROR) << "FAILURE: Extend PCR failed.";
      return false;
    }
  }

  LOG(INFO) << "Performing: Unseal, expected to "
            << (expected_after ? "succeed." : "fail.");
  auto out_data = storage.Unseal(*blob);
  if (expected_after) {
    if (!out_data) {
      LOG(ERROR) << "FAILURE: Unseal failed.";
      return false;
    }
    std::string result(out_data->begin(), out_data->end());
    if (result != kTestData) {
      LOG(ERROR) << "FAILURE: Unseal produced wrong data.";
      return false;
    }
  } else {
    if (out_data) {
      LOG(ERROR) << "FAILURE: Unseal unexpectedly succeeded.";
      return false;
    }
  }

  LOG(INFO) << "Expected final state: "
            << (expected_after ? "matches policy." : "doesn't match policy.");
  matches = storage.CheckState();
  if (!matches.has_value()) {
    LOG(ERROR) << "FAILURE: CheckState failed.";
    return false;
  }
  if (matches.value() != expected_after) {
    LOG(ERROR) << "FAILURE: Unexpected state.";
    return false;
  }

  LOG(INFO) << "Test PASSED.";
  return true;
}

int main(int argc, char** argv) {
  // Setup command line options.
  DEFINE_bool(syslog, false, "also log to syslog");

  DEFINE_bool(verified_boot, false, "policy: verified boot mode");
  DEFINE_bool(dev, false, "policy: dev mode");

  DEFINE_int32(policy_pcr, -1, "policy: unchanged PCR");
  DEFINE_int32(extend_pcr, -1, "PCR to extend");

  DEFINE_string(data, "/tmp/_test_data", "plaintext data file");
  DEFINE_string(blob, "/tmp/_sealed_storage_blob", "sealed blob file");

  DEFINE_bool(seal, false, "seal data");
  DEFINE_bool(unseal, false, "unseal data");
  DEFINE_bool(extend, false, "extend PCR");
  DEFINE_bool(check, false, "check if state matches policy");
  DEFINE_bool(test, false, "run a test with the specified boot mode");
  DEFINE_bool(wrapper, false, "use wrapper function when unsealing");

  brillo::FlagHelper::Init(argc, argv, "sealed_storage_tool");

  // Setup log.
  int log_flags = brillo::kLogToStderrIfTty;
  if (FLAGS_syslog) {
    log_flags |= brillo::kLogToSyslog;
  }
  brillo::InitLog(log_flags);

  // Make sure we have a message loop.
  base::SingleThreadTaskExecutor task_executor(base::MessagePumpType::IO);

  // Check provided command line.
  if (FLAGS_dev && FLAGS_verified_boot) {
    LOG(ERROR) << "Must select one boot mode between dev and verified_boot";
    return 1;
  }
  if (FLAGS_test &&
      (FLAGS_seal || FLAGS_unseal || FLAGS_extend || FLAGS_check)) {
    LOG(ERROR) << "Must select between running a test or a set of operations";
    return 1;
  }
  if (FLAGS_policy_pcr == 0) {
    LOG(ERROR) << "Policy PCR cannot be 0";
    return 1;
  }

  // Create the right object.
  sealed_storage::Policy policy =
      ConstructPolicy(FLAGS_verified_boot, FLAGS_dev, FLAGS_policy_pcr);
  sealed_storage::SealedStorage storage(policy);

  if (FLAGS_test) {
    if (!RunTest(storage, -1, true, true)) {
      return 1;
    }

    if (FLAGS_extend_pcr >= 0) {
      bool expected;
      if (FLAGS_extend_pcr == 0) {
        expected = !(FLAGS_verified_boot || FLAGS_dev);
      } else {
        expected = !(FLAGS_extend_pcr == FLAGS_policy_pcr);
      }
      if (!RunTest(storage, FLAGS_extend_pcr, true, expected)) {
        return 1;
      }

      if (!RunTest(storage, -1, expected, expected)) {
        return 1;
      }
    }
    LOG(INFO) << "ALL TESTS PASSED.";
    return 0;
  }

  if (FLAGS_check) {
    auto matches = storage.CheckState();
    if (!matches.has_value()) {
      return 1;
    }
    LOG(INFO) << "State matches policy: "
              << (matches.value() ? "true" : "false");
  }

  if (FLAGS_seal) {
    std::string input;
    if (!base::ReadFileToString(base::FilePath(FLAGS_data), &input)) {
      LOG(ERROR) << "Failed to read from " << FLAGS_data;
      return 1;
    }
    sealed_storage::SecretData data(input.begin(), input.end());
    auto blob = storage.Seal(data);
    if (!blob) {
      return 1;
    }
    std::string output(blob->begin(), blob->end());
    if (!brillo::WriteStringToFile(base::FilePath(FLAGS_blob), output)) {
      LOG(ERROR) << "Failed to write to " << FLAGS_blob;
      return 1;
    }
    LOG(INFO) << "Seal: success";
  }

  if (FLAGS_extend) {
    if (FLAGS_extend_pcr < 0) {
      LOG(ERROR) << "Need to specify PCR to extend";
      return 1;
    }
    if (!storage.ExtendPCR(FLAGS_extend_pcr)) {
      return 1;
    }
    LOG(INFO) << "PCR extend: success";
  }

  if (FLAGS_unseal) {
    std::string input;
    if (!base::ReadFileToString(base::FilePath(FLAGS_blob), &input)) {
      LOG(ERROR) << "Failed to read from " << FLAGS_blob;
      return 1;
    }
    std::string output;
    if (FLAGS_wrapper) {
      std::vector<uint8_t> blob(input.begin(), input.end());
      std::vector<uint8_t> data;
      bool verified_boot_mode = false;
      if (FLAGS_verified_boot) {
        verified_boot_mode = true;
      } else if (FLAGS_dev) {
        LOG(ERROR) << "dev mode not supported with wrapper";
        return 1;
      }

      size_t output_size = 2 * blob.size();
      data.resize(output_size);
      if (!sealed_storage::wrapper::Unseal(verified_boot_mode, FLAGS_policy_pcr,
                                           blob.data(), blob.size(),
                                           data.data(), &output_size)) {
        return 1;
      }
      data.resize(output_size);
      output = std::string(data.data(), data.data() + data.size());
    } else {
      sealed_storage::Data blob(input.begin(), input.end());
      auto data = storage.Unseal(blob);
      if (!data) {
        return 1;
      }
      output = std::string(data->begin(), data->end());
    }
    if (!brillo::WriteStringToFile(base::FilePath(FLAGS_data), output)) {
      LOG(ERROR) << "Failed to write to " << FLAGS_data;
      return 1;
    }
    LOG(INFO) << "Unseal: success";
  }

  return 0;
}
