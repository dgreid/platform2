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
#include "hardware_verifier/test_utils.h"
#include "hardware_verifier/verifier_fake.h"

using ::testing::_;
using ::testing::AtLeast;

namespace hardware_verifier {

class CLITest : public testing::Test {
 protected:
  void SetUp() override {
    pr_getter_ = new FakeProbeResultGetter();
    vp_getter_ = new FakeHwVerificationSpecGetter();
    verifier_ = new FakeVerifier();
    output_stream_.reset(new std::ostringstream());

    auto metrics = std::make_unique<MetricsLibraryMock>();
    metrics_ = metrics.get();
    Observer::GetInstance()->SetMetricsLibrary(std::move(metrics));

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

  void TearDown() override {
    // We have to clear the MetricsLibraryMock manually, because
    // Observer::GetInstance() object is a singleton, which won't be destroyed
    // across the tests.
    Observer::GetInstance()->SetMetricsLibrary(nullptr);
  }

  std::unique_ptr<CLI> cli_;
  FakeProbeResultGetter* pr_getter_;
  FakeHwVerificationSpecGetter* vp_getter_;
  FakeVerifier* verifier_;
  std::unique_ptr<std::ostringstream> output_stream_;
  MetricsLibraryMock* metrics_;
};

TEST_F(CLITest, TestBasicFlow) {
  EXPECT_EQ(cli_->Run("", "", CLIOutputFormat::kProtoBin, false),
            CLIVerificationResult::kPass);
}

TEST_F(CLITest, TestHandleWaysToGetProbeResults) {
  pr_getter_->set_runtime_probe_fail();
  EXPECT_EQ(cli_->Run("", "", CLIOutputFormat::kProtoBin, false),
            CLIVerificationResult::kProbeFail);

  pr_getter_->set_file_probe_results({{"path", runtime_probe::ProbeResult()}});
  EXPECT_EQ(cli_->Run("path", "", CLIOutputFormat::kProtoBin, false),
            CLIVerificationResult::kPass);
  EXPECT_EQ(cli_->Run("path2", "", CLIOutputFormat::kProtoBin, false),
            CLIVerificationResult::kInvalidProbeResultFile);
}

TEST_F(CLITest, TestHandleWaysToGetHwVerificationSpec) {
  vp_getter_->SetDefaultInvalid();
  EXPECT_EQ(cli_->Run("", "", CLIOutputFormat::kProtoBin, false),
            CLIVerificationResult::kInvalidHwVerificationSpecFile);

  vp_getter_->set_files({{"path", HwVerificationSpec()}});
  EXPECT_EQ(cli_->Run("", "path", CLIOutputFormat::kProtoBin, false),
            CLIVerificationResult::kPass);
  EXPECT_EQ(cli_->Run("", "path2", CLIOutputFormat::kProtoBin, false),
            CLIVerificationResult::kInvalidHwVerificationSpecFile);
}

TEST_F(CLITest, TestVerifyFail) {
  verifier_->SetVerifyFail();
  EXPECT_EQ(cli_->Run("", "", CLIOutputFormat::kProtoBin, false),
            CLIVerificationResult::kProbeResultHwVerificationSpecMisalignment);

  HwVerificationReport vr;
  vr.set_is_compliant(false);
  verifier_->SetVerifySuccess(vr);
  EXPECT_EQ(cli_->Run("", "", CLIOutputFormat::kProtoBin, false),
            CLIVerificationResult::kFail);
}

TEST_F(CLITest, TestOutput) {
  HwVerificationReport vr;
  vr.set_is_compliant(true);

  verifier_->SetVerifySuccess(vr);
  EXPECT_EQ(cli_->Run("", "", CLIOutputFormat::kProtoBin, true),
            CLIVerificationResult::kPass);
  HwVerificationReport result;
  EXPECT_TRUE(result.ParseFromString(output_stream_->str()));
  EXPECT_TRUE(google::protobuf::util::MessageDifferencer::Equals(result, vr));

  // For human readable format, only check if there's something printed.
  *output_stream_ = std::ostringstream();
  EXPECT_EQ(cli_->Run("", "", CLIOutputFormat::kText, false),
            CLIVerificationResult::kPass);
  EXPECT_FALSE(output_stream_->str().empty());

  *output_stream_ = std::ostringstream();
  EXPECT_EQ(cli_->Run("", "", CLIOutputFormat::kText, true),
            CLIVerificationResult::kPass);
  EXPECT_FALSE(output_stream_->str().empty());
}

TEST_F(CLITest, TestVerifyReportSample1) {
  const auto& path = GetTestDataPath()
                         .Append("verifier_impl_sample_data")
                         .Append("expect_hw_verification_report_1.prototxt");

  const auto& vr = LoadHwVerificationReport(path);
  verifier_->SetVerifySuccess(vr);

  // This is for recording running time.
  EXPECT_CALL(*metrics_, SendToUMA(_, _, _, _, _)).Times(AtLeast(1));
  EXPECT_CALL(
      *metrics_,
      SendBoolToUMA("ChromeOS.HardwareVerifier.Report.IsCompliant", true));
  // This is for recording qualification status of each components.
  EXPECT_CALL(*metrics_, SendEnumToUMA(_, _, _)).Times(AtLeast(3));

  EXPECT_EQ(cli_->Run("", "", CLIOutputFormat::kText, false),
            CLIVerificationResult::kPass);
}

TEST_F(CLITest, TestVerifyReportSample2) {
  const auto& path = GetTestDataPath()
                         .Append("verifier_impl_sample_data")
                         .Append("expect_hw_verification_report_2.prototxt");

  const auto& vr = LoadHwVerificationReport(path);
  verifier_->SetVerifySuccess(vr);

  // This is for recording running time.
  EXPECT_CALL(*metrics_, SendToUMA(_, _, _, _, _)).Times(AtLeast(1));
  EXPECT_CALL(
      *metrics_,
      SendBoolToUMA("ChromeOS.HardwareVerifier.Report.IsCompliant", false));
  // This is for recording qualification status of each components.
  EXPECT_CALL(*metrics_, SendEnumToUMA(_, _, _)).Times(AtLeast(2));

  EXPECT_EQ(cli_->Run("", "", CLIOutputFormat::kText, false),
            CLIVerificationResult::kFail);
}

}  // namespace hardware_verifier
