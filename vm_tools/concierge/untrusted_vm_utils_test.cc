// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/untrusted_vm_utils.h"

#include <memory>
#include <string>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/logging.h>
#include <base/macros.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/mock_bus.h>
#include <dbus/mock_object_proxy.h>
#include <dbus/scoped_dbus_error.h>

using ::testing::_;
using ::testing::Invoke;
using ::testing::Return;

namespace vm_tools {
namespace concierge {

namespace {

// The minimum kernel version of the host which supports untrusted VMs or a
// trusted VM with nested VM support.
constexpr KernelVersionAndMajorRevision kMinKernelVersion =
    std::make_pair(4, 10);

}  // namespace

// Test fixture for actually testing the VirtualMachine functionality.
class UntrustedVMUtilsTest : public ::testing::Test {
 public:
  UntrustedVMUtilsTest() {
    dbus::Bus::Options opts;
    mock_bus_ = new dbus::MockBus(opts);
    debugd_proxy_ =
        new dbus::MockObjectProxy(mock_bus_.get(), debugd::kDebugdServiceName,
                                  dbus::ObjectPath(debugd::kDebugdServicePath));

    // Sets an expectation that the mock proxy's
    // CallMethodAndBlockWithErrorDetails() will use CreateMockProxyResponse()
    // to return responses.
    EXPECT_CALL(*debugd_proxy_.get(),
                CallMethodAndBlockWithErrorDetails(_, _, _))
        .WillRepeatedly(
            Invoke(this, &UntrustedVMUtilsTest::CreateMockProxyResponse));
  }

  ~UntrustedVMUtilsTest() override = default;

  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    l1tf_status_path_ = temp_dir_.GetPath().Append("l1tf");
    mds_status_path_ = temp_dir_.GetPath().Append("mds");

    // Set a kernel version that supports untrusted VMs by default. Individual
    // test cases can override this if testing for related error scenarios.
    untrusted_vm_utils_ = std::make_unique<UntrustedVMUtils>(
        debugd_proxy_.get(), kMinKernelVersion, kMinKernelVersion,
        l1tf_status_path_, mds_status_path_);
  }

 protected:
  // Checks if |l1tf_status| yields |expected_status| when
  // |CheckUntrustedVMMitigationStatus| is called.
  void CheckL1TFStatus(const std::string& l1tf_status,
                       UntrustedVMUtils::MitigationStatus expected_status) {
    ASSERT_EQ(base::WriteFile(l1tf_status_path_, l1tf_status.c_str(),
                              l1tf_status.size()),
              l1tf_status.size());
    EXPECT_EQ(untrusted_vm_utils_->CheckUntrustedVMMitigationStatus(),
              expected_status);
  }

  // Checks if |mds_status| yields |expected_status| when
  // |CheckUntrustedVMMitigationStatus| is called.
  void CheckMDSStatus(const std::string& mds_status,
                      UntrustedVMUtils::MitigationStatus expected_status) {
    ASSERT_EQ(base::WriteFile(mds_status_path_, mds_status.c_str(),
                              mds_status.size()),
              mds_status.size());
    EXPECT_EQ(untrusted_vm_utils_->CheckUntrustedVMMitigationStatus(),
              expected_status);
  }

  // Directory and file path used for reading test vulnerability statuses.
  base::ScopedTempDir temp_dir_;
  base::FilePath l1tf_status_path_;
  base::FilePath mds_status_path_;

  std::unique_ptr<UntrustedVMUtils> untrusted_vm_utils_;

 private:
  std::unique_ptr<dbus::Response> CreateMockProxyResponse(
      dbus::MethodCall* method_call,
      int timeout_ms,
      dbus::ScopedDBusError* error) {
    if (method_call->GetInterface() != debugd::kDebugdInterface) {
      LOG(ERROR) << "Unexpected method call: " << method_call->ToString();
      return std::unique_ptr<dbus::Response>();
    }

    std::unique_ptr<dbus::Response> response = dbus::Response::CreateEmpty();
    if (method_call->GetMember() != debugd::kSetSchedulerConfigurationV2) {
      LOG(ERROR) << "Unexpected method call: " << method_call->ToString();
      return std::unique_ptr<dbus::Response>();
    }

    dbus::MessageWriter writer(response.get());
    writer.AppendBool(true);
    return response;
  }

  scoped_refptr<dbus::MockBus> mock_bus_;
  scoped_refptr<dbus::MockObjectProxy> debugd_proxy_;

  DISALLOW_COPY_AND_ASSIGN(UntrustedVMUtilsTest);
};

// Check if lower kernel versions always yield |VULNERABLE| status.
TEST_F(UntrustedVMUtilsTest, CheckLowerKernelVersion) {
  untrusted_vm_utils_->SetKernelVersionForTesting(
      std::make_pair(kMinKernelVersion.first, kMinKernelVersion.second - 1));
  EXPECT_EQ(untrusted_vm_utils_->CheckUntrustedVMMitigationStatus(),
            UntrustedVMUtils::MitigationStatus::VULNERABLE);

  untrusted_vm_utils_->SetKernelVersionForTesting(std::make_pair(
      kMinKernelVersion.first - 1, kMinKernelVersion.second + 1));
  EXPECT_EQ(untrusted_vm_utils_->CheckUntrustedVMMitigationStatus(),
            UntrustedVMUtils::MitigationStatus::VULNERABLE);

  untrusted_vm_utils_->SetKernelVersionForTesting(std::make_pair(
      kMinKernelVersion.first - 1, kMinKernelVersion.second - 1));
  EXPECT_EQ(untrusted_vm_utils_->CheckUntrustedVMMitigationStatus(),
            UntrustedVMUtils::MitigationStatus::VULNERABLE);
}

// Checks mitigation status for all L1TF statuses.
TEST_F(UntrustedVMUtilsTest, CheckL1TFStatus) {
  // Set MDS status to be not vulnerable in order to check L1TF statuses below.
  std::string mds_status = "Mitigation: Clear CPU buffers; SMT disabled";
  ASSERT_EQ(
      base::WriteFile(mds_status_path_, mds_status.c_str(), mds_status.size()),
      mds_status.size());

  CheckL1TFStatus("Not affected",
                  UntrustedVMUtils::MitigationStatus::NOT_VULNERABLE);

  CheckL1TFStatus("Mitigation: PTE Inversion",
                  UntrustedVMUtils::MitigationStatus::NOT_VULNERABLE);

  CheckL1TFStatus("Some gibberish; some more gibberish",
                  UntrustedVMUtils::MitigationStatus::VULNERABLE);

  CheckL1TFStatus(
      "Mitigation: PTE Inversion; VMX: conditional cache flushes, SMT "
      "vulnerable",
      UntrustedVMUtils::MitigationStatus::VULNERABLE);

  CheckL1TFStatus(
      "Mitigation: PTE Inversion; VMX: cache flushes, SMT vulnerable",
      UntrustedVMUtils::MitigationStatus::VULNERABLE_DUE_TO_SMT_ENABLED);

  CheckL1TFStatus("Mitigation: PTE Inversion; VMX: cache flushes, SMT disabled",
                  UntrustedVMUtils::MitigationStatus::NOT_VULNERABLE);
}

// Checks mitigation status for all MDS statuses.
TEST_F(UntrustedVMUtilsTest, CheckMDSStatus) {
  // Set L1TF status to be not vulnerable in order to check MDS statuses below.
  std::string l1tf_status =
      "Mitigation: PTE Inversion; VMX: cache flushes, SMT "
      "disabled";
  ASSERT_EQ(base::WriteFile(l1tf_status_path_, l1tf_status.c_str(),
                            l1tf_status.size()),
            l1tf_status.size());

  CheckMDSStatus("Not affected",
                 UntrustedVMUtils::MitigationStatus::NOT_VULNERABLE);

  CheckMDSStatus("Some gibberish; some more gibberish",
                 UntrustedVMUtils::MitigationStatus::VULNERABLE);

  CheckMDSStatus("Vulnerable: Clear CPU buffers attempted; no microcode",
                 UntrustedVMUtils::MitigationStatus::VULNERABLE);

  CheckMDSStatus("Vulnerable; SMT disabled",
                 UntrustedVMUtils::MitigationStatus::VULNERABLE);

  CheckMDSStatus("Mitigation: Clear CPU buffers; SMT disabled",
                 UntrustedVMUtils::MitigationStatus::NOT_VULNERABLE);

  CheckMDSStatus(
      "Mitigation: Clear CPU buffers; SMT mitigated",
      UntrustedVMUtils::MitigationStatus::VULNERABLE_DUE_TO_SMT_ENABLED);

  CheckMDSStatus(
      "Mitigation: Clear CPU buffers; SMT vulnerable",
      UntrustedVMUtils::MitigationStatus::VULNERABLE_DUE_TO_SMT_ENABLED);

  CheckMDSStatus(
      "Mitigation: Clear CPU buffers; SMT Host state unknown",
      UntrustedVMUtils::MitigationStatus::VULNERABLE_DUE_TO_SMT_ENABLED);
}

// Checks if |DisableSMT| API makes a D-Bus call inside.
TEST_F(UntrustedVMUtilsTest, CheckDisableSMT) {
  EXPECT_TRUE(untrusted_vm_utils_->DisableSMT());
}

}  // namespace concierge
}  // namespace vm_tools
