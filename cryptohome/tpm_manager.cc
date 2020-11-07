// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <stdio.h>
#include <string>

#include <attestation/common/print_interface_proto.h>
#include <attestation/proto_bindings/interface.pb.h>
#include <base/command_line.h>
#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/macros.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <brillo/secure_blob.h>
#include <brillo/syslog_logging.h>
#include <openssl/evp.h>
#include <tpm_manager/common/print_tpm_manager_proto.h>
#include <tpm_manager/proto_bindings/tpm_manager.pb.h>

#include <attestation-client/attestation/dbus-proxies.h>
#include <tpm_manager-client/tpm_manager/dbus-proxies.h>

#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/tpm.h"

namespace cryptohome {

namespace tpm_manager {

void PrintUsage() {
  std::string program =
      base::CommandLine::ForCurrentProcess()->GetProgram().BaseName().value();
  printf("Usage: %s [command] [options]\n", program.c_str());
  printf("  Commands:\n");
  printf(
      "    initialize: Takes ownership of an unowned TPM and initializes it "
      "for use with Chrome OS Core. This is the default command.\n"
      "      - Install attributes will be empty and finalized.\n"
      "      - Attestation data will be prepared.\n"
      "      This command may be run safely multiple times and may be "
      "retried on failure. If the TPM is already initialized this command\n"
      "      has no effect and exits without error. The --finalize option "
      "will cause various TPM data to be finalized (this does not affect\n"
      "      install attributes which are always finalized).\n");
  printf(
      "    verify_endorsement: Verifies TPM endorsement.\n"
      "      If the --cros_core option is specified then Chrome OS Core "
      "endorsement is verified. Otherwise, normal Chromebook endorsement\n"
      "      is verified. Requires the TPM to be initialized but not "
      "finalized.\n");
  printf(
      "    get_random <N>: Gets N random bytes from the TPM and prints them "
      "as a hex-encoded string.\n");
  printf(
      "    get_version_info: Prints TPM software and hardware version "
      "information.\n");
  printf(
      "    get_ifx_field_upgrade_info: Prints status information pertaining "
      "to firmware updates on Infineon TPMs.\n");
  printf("    get_srk_status: Prints SRK status information.\n");
}

static void PrintIFXFirmwarePackage(
    const Tpm::IFXFieldUpgradeInfo::FirmwarePackage& firmware_package,
    const char* prefix) {
  printf("%s_package_id %08x\n", prefix, firmware_package.package_id);
  printf("%s_version %08x\n", prefix, firmware_package.version);
  printf("%s_stale_version %08x\n", prefix, firmware_package.stale_version);
}

int TakeOwnership(bool finalize) {
  base::Time start_time = base::Time::Now();

  dbus::Bus::Options options;
  options.bus_type = dbus::Bus::SYSTEM;
  org::chromium::TpmManagerProxy proxy(
      base::MakeRefCounted<dbus::Bus>(options));
  brillo::ErrorPtr error;

  LOG(INFO) << "Initializing TPM.";
  ::tpm_manager::TakeOwnershipRequest request;
  ::tpm_manager::TakeOwnershipReply reply;
  if (!proxy.TakeOwnership(request, &reply, &error)) {
    LOG(ERROR) << "Error sending dbus message to tpm_manager: "
               << error->GetMessage();
    return -1;
  }

  if (reply.status() != ::tpm_manager::STATUS_SUCCESS) {
    LOG(ERROR) << "Failed to take ownership.";
    puts(GetProtoDebugString(reply).c_str());
    return -1;
  }
  if (finalize) {
    LOG(WARNING) << "Finalization is ignored.";
  }
  base::TimeDelta duration = base::Time::Now() - start_time;
  LOG(INFO) << "TPM initialization successful (" << duration.InMilliseconds()
            << " ms).";
  return 0;
}

int VerifyEK(bool is_cros_core) {
  dbus::Bus::Options options;
  options.bus_type = dbus::Bus::SYSTEM;
  org::chromium::AttestationProxy proxy(
      base::MakeRefCounted<dbus::Bus>(options));
  brillo::ErrorPtr error;

  attestation::VerifyRequest request;
  request.set_cros_core(is_cros_core);
  request.set_ek_only(true);
  attestation::VerifyReply reply;
  if (!proxy.Verify(request, &reply, &error)) {
    LOG(ERROR) << "Error sending dbus message to tpm_manager: "
               << error->GetMessage();
    return -1;
  }
  if (reply.status() != attestation::AttestationStatus::STATUS_SUCCESS) {
    LOG(ERROR) << "Failed to verify TPM endorsement.";
    puts(GetProtoDebugString(reply).c_str());
    return -1;
  }
  if (!reply.verified()) {
    LOG(ERROR) << "TPM endorsement verification failed.";
    return -1;
  }
  LOG(INFO) << "TPM endorsement verified successfully.";
  return 0;
}

int GetRandom(unsigned int random_bytes_count) {
  cryptohome::Tpm* tpm = cryptohome::Tpm::GetSingleton();
  brillo::SecureBlob random_bytes;
  tpm->GetRandomDataSecureBlob(random_bytes_count, &random_bytes);
  if (random_bytes_count != random_bytes.size())
    return -1;

  std::string hex_bytes =
      base::HexEncode(random_bytes.data(), random_bytes.size());
  puts(hex_bytes.c_str());
  return 0;
}

bool GetVersionInfo(cryptohome::Tpm::TpmVersionInfo* version_info) {
  cryptohome::Tpm* tpm = cryptohome::Tpm::GetSingleton();
  return tpm->GetVersionInfo(version_info);
}

bool GetIFXFieldUpgradeInfo(cryptohome::Tpm::IFXFieldUpgradeInfo* info) {
  cryptohome::Tpm* tpm = cryptohome::Tpm::GetSingleton();
  return tpm->GetIFXFieldUpgradeInfo(info);
}

bool GetTpmStatus(cryptohome::Tpm::TpmStatusInfo* status) {
  cryptohome::Tpm* tpm = cryptohome::Tpm::GetSingleton();
  tpm->GetStatus(0, status);
  return true;
}

}  // namespace tpm_manager

}  // namespace cryptohome

using cryptohome::tpm_manager::GetIFXFieldUpgradeInfo;
using cryptohome::tpm_manager::GetRandom;
using cryptohome::tpm_manager::GetTpmStatus;
using cryptohome::tpm_manager::GetVersionInfo;
using cryptohome::tpm_manager::PrintIFXFirmwarePackage;
using cryptohome::tpm_manager::PrintUsage;
using cryptohome::tpm_manager::TakeOwnership;
using cryptohome::tpm_manager::VerifyEK;

int main(int argc, char** argv) {
  base::CommandLine::Init(argc, argv);
  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderr);
  OpenSSL_add_all_algorithms();
  cryptohome::ScopedMetricsInitializer metrics_initializer;
  base::CommandLine::StringVector arguments = command_line->GetArgs();
  std::string command;
  if (arguments.size() > 0) {
    command = arguments[0];
  }
  if (command_line->HasSwitch("h") || command_line->HasSwitch("help")) {
    PrintUsage();
    return 0;
  }
  if (command.empty() || command == "initialize") {
    return TakeOwnership(command_line->HasSwitch("finalize"));
  }
  if (command == "verify_endorsement") {
    return VerifyEK(command_line->HasSwitch("cros_core"));
  }
  unsigned int random_bytes_count = 0;
  if (command == "get_random" && arguments.size() == 2 &&
      base::StringToUint(arguments[1], &random_bytes_count) &&
      random_bytes_count > 0) {
    return GetRandom(random_bytes_count);
  }
  if (command == "get_version_info") {
    cryptohome::Tpm::TpmVersionInfo version_info;
    if (!GetVersionInfo(&version_info)) {
      return -1;
    }

    uint32_t fingerprint = version_info.GetFingerprint();
    std::string vendor_specific = base::ToLowerASCII(
        base::HexEncode(version_info.vendor_specific.data(),
                        version_info.vendor_specific.size()));
    printf("tpm_family %08" PRIx32
           "\n"
           "spec_level %016" PRIx64
           "\n"
           "vendor %08" PRIx32
           "\n"
           "tpm_model %08" PRIx32
           "\n"
           "firmware_version %016" PRIx64
           "\n"
           "vendor_specific %s\n"
           "version_fingerprint %" PRId32 " %08" PRIx32 "\n",
           version_info.family, version_info.spec_level,
           version_info.manufacturer, version_info.tpm_model,
           version_info.firmware_version, vendor_specific.c_str(), fingerprint,
           fingerprint);
    return 0;
  }
  if (command == "get_ifx_field_upgrade_info") {
    cryptohome::Tpm::IFXFieldUpgradeInfo info;
    if (!GetIFXFieldUpgradeInfo(&info)) {
      return -1;
    }

    printf("max_data_size %u\n", info.max_data_size);
    PrintIFXFirmwarePackage(info.bootloader, "bootloader");
    PrintIFXFirmwarePackage(info.firmware[0], "fw0");
    PrintIFXFirmwarePackage(info.firmware[1], "fw1");
    printf("status %04x\n", info.status);
    PrintIFXFirmwarePackage(info.process_fw, "process_fw");
    printf("field_upgrade_counter %u\n", info.field_upgrade_counter);

    return 0;
  }
  if (command == "get_srk_status") {
    cryptohome::Tpm::TpmStatusInfo status;
    if (!GetTpmStatus(&status)) {
      return -1;
    }

    printf(
        "can_connect %d\n"
        "can_load_srk %d\n"
        "can_load_srk_public_key %d\n"
        "srk_vulnerable_roca %d\n",
        status.can_connect, status.can_load_srk, status.can_load_srk_public_key,
        status.srk_vulnerable_roca);
    return 0;
  }
  PrintUsage();
  return -1;
}
