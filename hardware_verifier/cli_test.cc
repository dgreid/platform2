/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <memory>
#include <sstream>
#include <utility>

#include <google/protobuf/util/message_differencer.h>
#include <gtest/gtest.h>
#include <metrics/metrics_library_mock.h>
#include <runtime_probe/proto_bindings/runtime_probe.pb.h>

#include "hardware_verifier/cli.h"
#include "hardware_verifier/hardware_verifier.pb.h"
#include "hardware_verifier/hw_verification_spec_getter_fake.h"
#include "hardware_verifier/observer.h"
#include "hardware_verifier/probe_result_getter_fake.h"
#include "hardware_verifier/verifier_fake.h"

namespace hardware_verifier {

class CLITest : public testing::Test {
 protected:
  void SetUp() {
    pr_getter_ = new FakeProbeResultGetter();
    vp_getter_ = new FakeHwVerificationSpecGetter();
    verifier_ = new FakeVerifier();
    output_stream_.reset(new std::ostringstream());

    Observer::GetInstance()->SetMetricsLibrary(
        std::make_unique<MetricsLibraryMock>());

    cli_ = std::make_unique<CLI>();
    cli_->pr_getter_.reset(pr_getter_);
    cli_->vp_getter_.reset(vp_getter_);
    cli_->verifier_.reset(verifier_);
    cli_->output_stream_ = output_stream_.get();

    // set everything works by default.
    pr_getter_->set_runtime_probe_output(runtime_probe::ProbeResult());
    vp_getter_->set_default(HwVerificationSpec());
    HwVerificationReport positive_report;
    positive_report.set_is_compliant(true);
    verifier_->SetVerifySuccess(positive_report);
  }

  std::unique_ptr<CLI> cli_;
  FakeProbeResultGetter* pr_getter_;
  FakeHwVerificationSpecGetter* vp_getter_;
  FakeVerifier* verifier_;
  std::unique_ptr<std::ostringstream> output_stream_;
};

TEST_F(CLITest, TestBasicFlow) {
  EXPECT_EQ(cli_->Run("", "", CLIOutputFormat::kProtoBin),
            CLIVerificationResult::kPass);
}

TEST_F(CLITest, TestHandleWaysToGetProbeResults) {
  pr_getter_->set_runtime_probe_fail();
  EXPECT_EQ(cli_->Run("", "", CLIOutputFormat::kProtoBin),
            CLIVerificationResult::kProbeFail);

  pr_getter_->set_file_probe_results({{"path", runtime_probe::ProbeResult()}});
  EXPECT_EQ(cli_->Run("path", "", CLIOutputFormat::kProtoBin),
            CLIVerificationResult::kPass);
  EXPECT_EQ(cli_->Run("path2", "", CLIOutputFormat::kProtoBin),
            CLIVerificationResult::kInvalidProbeResultFile);
}

TEST_F(CLITest, TestHandleWaysToGetHwVerificationSpec) {
  vp_getter_->SetDefaultInvalid();
  EXPECT_EQ(cli_->Run("", "", CLIOutputFormat::kProtoBin),
            CLIVerificationResult::kInvalidHwVerificationSpecFile);

  vp_getter_->set_files({{"path", HwVerificationSpec()}});
  EXPECT_EQ(cli_->Run("", "path", CLIOutputFormat::kProtoBin),
            CLIVerificationResult::kPass);
  EXPECT_EQ(cli_->Run("", "path2", CLIOutputFormat::kProtoBin),
            CLIVerificationResult::kInvalidHwVerificationSpecFile);
}

TEST_F(CLITest, TestVerifyFail) {
  verifier_->SetVerifyFail();
  EXPECT_EQ(cli_->Run("", "", CLIOutputFormat::kProtoBin),
            CLIVerificationResult::kProbeResultHwVerificationSpecMisalignment);

  HwVerificationReport vr;
  vr.set_is_compliant(false);
  verifier_->SetVerifySuccess(vr);
  EXPECT_EQ(cli_->Run("", "", CLIOutputFormat::kProtoBin),
            CLIVerificationResult::kFail);
}

TEST_F(CLITest, TestOutput) {
  HwVerificationReport vr;
  vr.set_is_compliant(true);

  verifier_->SetVerifySuccess(vr);
  EXPECT_EQ(cli_->Run("", "", CLIOutputFormat::kProtoBin),
            CLIVerificationResult::kPass);
  HwVerificationReport result;
  EXPECT_TRUE(result.ParseFromString(output_stream_->str()));
  EXPECT_TRUE(google::protobuf::util::MessageDifferencer::Equals(result, vr));

  // For human readable format, only check if there's something printed.
  *output_stream_ = std::ostringstream();
  EXPECT_EQ(cli_->Run("", "", CLIOutputFormat::kText),
            CLIVerificationResult::kPass);
  EXPECT_FALSE(output_stream_->str().empty());
}

}  // namespace hardware_verifier
