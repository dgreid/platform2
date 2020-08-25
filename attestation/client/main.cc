// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <sysexits.h>

#include <memory>
#include <string>

#include <attestation/proto_bindings/attestation_ca.pb.h>
#include <attestation/proto_bindings/interface.pb.h>
#include <base/bind.h>
#include <base/command_line.h>
#include <base/files/file_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/threading/thread_task_runner_handle.h>
#include <brillo/daemons/daemon.h>
#include <brillo/syslog_logging.h>

#include "attestation/client/dbus_proxy.h"
#include "attestation/common/crypto_utility_impl.h"
#include "attestation/common/print_interface_proto.h"

namespace attestation {

const char kCreateCommand[] = "create";
const char kInfoCommand[] = "info";
const char kSetKeyPayloadCommand[] = "set_key_payload";
const char kDeleteKeysCommand[] = "delete_keys";
const char kEndorsementCommand[] = "endorsement";
const char kAttestationKeyCommand[] = "attestation_key";
const char kVerifyAttestationCommand[] = "verify_attestation";
const char kActivateCommand[] = "activate";
const char kEncryptForActivateCommand[] = "encrypt_for_activate";
const char kEncryptCommand[] = "encrypt";
const char kDecryptCommand[] = "decrypt";
const char kSignCommand[] = "sign";
const char kVerifyCommand[] = "verify";
const char kRegisterCommand[] = "register";
const char kStatusCommand[] = "status";
const char kCreateEnrollRequestCommand[] = "create_enroll_request";
const char kFinishEnrollCommand[] = "finish_enroll";
const char kEnrollCommand[] = "enroll";
const char kCreateCertRequestCommand[] = "create_cert_request";
const char kFinishCertRequestCommand[] = "finish_cert_request";
const char kGetCertCommand[] = "get_cert";
const char kSignChallengeCommand[] = "sign_challenge";
const char kGetEnrollmentId[] = "get_enrollment_id";
const char kGetCertifiedNvIndex[] = "get_certified_nv_index";
const char kUsage[] = R"(
Usage: attestation_client <command> [<args>]
Commands:
  create [--user=<email>] [--label=<keylabel>] [--usage=sign|decrypt]
      Creates a certifiable key.
  set_key_payload [--user=<email>] --label=<keylabel> --input=<input_file>
      Reads payload from |input_file| and sets it for the specified key.
  delete_keys [--user=<email>]  --prefix=<prefix>
      Deletes all keys with the specified |prefix|.

  status [--extended]
      Requests and prints status or extended status: prepared_for_enrollment,
      enrolled, verified_boot [extended].
  info [--user=<email>] [--label=<keylabel>]
      Prints info about a key.
  endorsement
      Prints info about the TPM endorsement.
  attestation_key
      Prints info about the TPM attestation key.
  verify_attestation [--ek-only] [--cros-core]
      Verifies attestation information. If |ek-only| flag is provided,
      verifies only the endorsement key. If |cros-core| flag is provided,
      verifies using CrosCore CA public key.

  activate [--attestation-server=default|test] --input=<input_file> [--save]
      Activates an attestation key using the encrypted credential in
      |input_file| and optionally saves it for future certifications.
  encrypt_for_activate --input=<input_file> --output=<output_file>
      Encrypts the content of |input_file| as required by the TPM for
      activating an attestation key. The result is written to |output_file|.

  encrypt [--user=<email>] [--label=<keylabel>] --input=<input_file>
          --output=<output_file>
      Encrypts the contents of |input_file| as required by the TPM for a
      decrypt operation. The result is written to |output_file|.
  decrypt [--user=<email>] [--label=<keylabel>] --input=<input_file>
      Decrypts the contents of |input_file|.

  sign [--user=<email>] [--label=<keylabel>] --input=<input_file>
          [--output=<output_file>]
      Signs the contents of |input_file|.
  verify [--user=<email>] [--label=<keylabel] --input=<signed_data_file>
          --signature=<signature_file>
      Verifies the signature in |signature_file| against the contents of
      |input_file|.

  create_enroll_request [--attestation-server=default|test]
          [--output=<output_file>]
      Creates enroll request to CA and stores it to |output_file|.
  finish_enroll [--attestation-server=default|test] --input=<input_file>
      Finishes enrollment using the CA response from |input_file|.
  create_enroll_request [--attestation-server=default|test]
      Enrolls the device to the specified CA.
  create_cert_request [--attestation-server=default|test]
        [--profile=<profile>] [--user=<user>] [--origin=<origin>]
        [--output=<output_file>]
      Creates certificate request to CA for |user|, using provided certificate
        |profile| and |origin|, and stores it to |output_file|.
        Possible |profile| values: user, machine, enrollment, content, cpsi,
        cast, gfsc. Default is user.
  finish_cert_request [--attestation-server=default|test] [--user=<user>]
          [--label=<label>] --input=<input_file>
      Finishes certificate request for |user| using the CA response from
      |input_file|, and stores it in the key with the specified |label|.
  get_cert [--attestation-server=default|test] [--profile=<profile>]
        [--label=<label>] [--user=<user>] [--origin=<origin>]
        [--output=<output_file>]
      Creates certificate request to CA for |user|, using provided certificate
      |profile| and |origin|, and sends to the specified CA, then stores it
      with the specified |label|.
      Possible |profile| values: user, machine, enrollment, content, cpsi,
      cast, gfsc. Default is user.
  sign_challenge [--enterprise [--va_server=default|test]] [--user=<user>]
          [--label=<label>] [--domain=<domain>] [--device_id=<device_id>]
          [--spkac] --input=<input_file> [--output=<output_file>]
      Signs a challenge (EnterpriseChallenge, if |enterprise| flag is given,
        otherwise a SimpleChallenge) provided in the |input_file|. Stores
        the response in the |output_file|, if specified.

  register [--user=<email>] [--label=<keylabel]
      Registers a key with a PKCS #11 token.

  get_enrollment_id [--ignore_cache]
      Returns the enrollment ID. If ignore_cache option is provided, the ID is
        computed and the cache is not used to read, nor to update the value.
        Otherwise the value from cache is returned if present.

  get_certified_nv_index [--index=<nv_index>] [--size=<bytes>]
          [--key=<key_label>] [--output=<output_file>]
      Returns a copy of the specified NV index, certified by the specified
      key, eg "attest-ent-machine".
)";

// The Daemon class works well as a client loop as well.
using ClientLoopBase = brillo::Daemon;

class ClientLoop : public ClientLoopBase {
 public:
  ClientLoop() = default;
  ~ClientLoop() override = default;

 protected:
  int OnInit() override {
    int exit_code = ClientLoopBase::OnInit();
    if (exit_code != EX_OK) {
      return exit_code;
    }
    attestation_.reset(new attestation::DBusProxy());
    if (!attestation_->Initialize()) {
      return EX_UNAVAILABLE;
    }
    exit_code = ScheduleCommand();
    if (exit_code == EX_USAGE) {
      printf("%s", kUsage);
    }
    return exit_code;
  }

  void OnShutdown(int* exit_code) override {
    attestation_.reset();
    ClientLoopBase::OnShutdown(exit_code);
  }

 private:
  // Posts tasks according to the command line options.
  int ScheduleCommand() {
    base::Closure task;
    base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
    const auto& args = command_line->GetArgs();
    if (command_line->HasSwitch("help") || command_line->HasSwitch("h") ||
        args.empty() || (!args.empty() && args.front() == "help")) {
      return EX_USAGE;
    } else if (args.front() == kCreateCommand) {
      std::string usage_str = command_line->GetSwitchValueASCII("usage");
      KeyUsage usage;
      if (usage_str.empty() || usage_str == "sign") {
        usage = KEY_USAGE_SIGN;
      } else if (usage_str == "decrypt") {
        usage = KEY_USAGE_DECRYPT;
      } else {
        return EX_USAGE;
      }
      task = base::Bind(&ClientLoop::CallCreateCertifiableKey,
                        weak_factory_.GetWeakPtr(),
                        command_line->GetSwitchValueASCII("label"),
                        command_line->GetSwitchValueASCII("user"), usage);
    } else if (args.front() == kStatusCommand) {
      task = base::Bind(&ClientLoop::CallGetStatus, weak_factory_.GetWeakPtr(),
                        command_line->HasSwitch("extended"));
    } else if (args.front() == kInfoCommand) {
      task = base::Bind(&ClientLoop::CallGetKeyInfo, weak_factory_.GetWeakPtr(),
                        command_line->GetSwitchValueASCII("label"),
                        command_line->GetSwitchValueASCII("user"));
    } else if (args.front() == kSetKeyPayloadCommand) {
      if (!command_line->HasSwitch("input")) {
        return EX_USAGE;
      }
      std::string input;
      base::FilePath filename(command_line->GetSwitchValueASCII("input"));
      if (!base::ReadFileToString(filename, &input)) {
        LOG(ERROR) << "Failed to read file: " << filename.value();
        return EX_NOINPUT;
      }
      task =
          base::Bind(&ClientLoop::CallSetKeyPayload, weak_factory_.GetWeakPtr(),
                     input, command_line->GetSwitchValueASCII("label"),
                     command_line->GetSwitchValueASCII("user"));
    } else if (args.front() == kDeleteKeysCommand) {
      if (command_line->HasSwitch("label") &&
          command_line->HasSwitch("prefix")) {
        return EX_USAGE;
      }
      task = base::Bind(&ClientLoop::CallDeleteKeys, weak_factory_.GetWeakPtr(),
                        command_line->GetSwitchValueASCII("prefix"),
                        command_line->GetSwitchValueASCII("label"),
                        command_line->GetSwitchValueASCII("user"));
    } else if (args.front() == kEndorsementCommand) {
      task = base::Bind(&ClientLoop::CallGetEndorsementInfo,
                        weak_factory_.GetWeakPtr());
    } else if (args.front() == kAttestationKeyCommand) {
      ACAType aca_type;
      int status = GetCertificateAuthorityServerType(command_line, &aca_type);
      if (status != EX_OK) {
        return status;
      }
      task = base::Bind(&ClientLoop::CallGetAttestationKeyInfo,
                        weak_factory_.GetWeakPtr(), aca_type);
    } else if (args.front() == kVerifyAttestationCommand) {
      task = base::Bind(&ClientLoop::CallVerifyAttestation,
                        weak_factory_.GetWeakPtr(),
                        command_line->HasSwitch("cros-core"),
                        command_line->HasSwitch("ek-only"));
    } else if (args.front() == kActivateCommand) {
      ACAType aca_type;
      int status = GetCertificateAuthorityServerType(command_line, &aca_type);
      if (status != EX_OK) {
        return status;
      }
      if (!command_line->HasSwitch("input")) {
        return EX_USAGE;
      }
      std::string input;
      base::FilePath filename(command_line->GetSwitchValueASCII("input"));
      if (!base::ReadFileToString(filename, &input)) {
        LOG(ERROR) << "Failed to read file: " << filename.value();
        return EX_NOINPUT;
      }
      task = base::Bind(&ClientLoop::CallActivateAttestationKey,
                        weak_factory_.GetWeakPtr(), aca_type, input,
                        command_line->HasSwitch("save"));
    } else if (args.front() == kEncryptForActivateCommand) {
      if (!command_line->HasSwitch("input") ||
          !command_line->HasSwitch("output")) {
        return EX_USAGE;
      }
      std::string input;
      base::FilePath filename(command_line->GetSwitchValueASCII("input"));
      if (!base::ReadFileToString(filename, &input)) {
        LOG(ERROR) << "Failed to read file: " << filename.value();
        return EX_NOINPUT;
      }
      task = base::Bind(&ClientLoop::EncryptForActivate,
                        weak_factory_.GetWeakPtr(), input);
    } else if (args.front() == kEncryptCommand) {
      if (!command_line->HasSwitch("input") ||
          !command_line->HasSwitch("output")) {
        return EX_USAGE;
      }
      std::string input;
      base::FilePath filename(command_line->GetSwitchValueASCII("input"));
      if (!base::ReadFileToString(filename, &input)) {
        LOG(ERROR) << "Failed to read file: " << filename.value();
        return EX_NOINPUT;
      }
      task = base::Bind(&ClientLoop::Encrypt, weak_factory_.GetWeakPtr(),
                        command_line->GetSwitchValueASCII("label"),
                        command_line->GetSwitchValueASCII("user"), input);
    } else if (args.front() == kDecryptCommand) {
      if (!command_line->HasSwitch("input")) {
        return EX_USAGE;
      }
      std::string input;
      base::FilePath filename(command_line->GetSwitchValueASCII("input"));
      if (!base::ReadFileToString(filename, &input)) {
        LOG(ERROR) << "Failed to read file: " << filename.value();
        return EX_NOINPUT;
      }
      task = base::Bind(&ClientLoop::CallDecrypt, weak_factory_.GetWeakPtr(),
                        command_line->GetSwitchValueASCII("label"),
                        command_line->GetSwitchValueASCII("user"), input);
    } else if (args.front() == kSignCommand) {
      if (!command_line->HasSwitch("input")) {
        return EX_USAGE;
      }
      std::string input;
      base::FilePath filename(command_line->GetSwitchValueASCII("input"));
      if (!base::ReadFileToString(filename, &input)) {
        LOG(ERROR) << "Failed to read file: " << filename.value();
        return EX_NOINPUT;
      }
      task = base::Bind(&ClientLoop::CallSign, weak_factory_.GetWeakPtr(),
                        command_line->GetSwitchValueASCII("label"),
                        command_line->GetSwitchValueASCII("user"), input);
    } else if (args.front() == kVerifyCommand) {
      if (!command_line->HasSwitch("input") ||
          !command_line->HasSwitch("signature")) {
        return EX_USAGE;
      }
      std::string input;
      base::FilePath filename(command_line->GetSwitchValueASCII("input"));
      if (!base::ReadFileToString(filename, &input)) {
        LOG(ERROR) << "Failed to read file: " << filename.value();
        return EX_NOINPUT;
      }
      std::string signature;
      base::FilePath filename2(command_line->GetSwitchValueASCII("signature"));
      if (!base::ReadFileToString(filename2, &signature)) {
        LOG(ERROR) << "Failed to read file: " << filename2.value();
        return EX_NOINPUT;
      }
      task = base::Bind(
          &ClientLoop::VerifySignature, weak_factory_.GetWeakPtr(),
          command_line->GetSwitchValueASCII("label"),
          command_line->GetSwitchValueASCII("user"), input, signature);
    } else if (args.front() == kRegisterCommand) {
      task = base::Bind(&ClientLoop::CallRegister, weak_factory_.GetWeakPtr(),
                        command_line->GetSwitchValueASCII("label"),
                        command_line->GetSwitchValueASCII("user"));
    } else if (args.front() == kCreateEnrollRequestCommand) {
      ACAType aca_type;
      int status = GetCertificateAuthorityServerType(command_line, &aca_type);
      if (status != EX_OK) {
        return status;
      }
      task = base::Bind(&ClientLoop::CallCreateEnrollRequest,
                        weak_factory_.GetWeakPtr(), aca_type);
    } else if (args.front() == kFinishEnrollCommand) {
      ACAType aca_type;
      int status = GetCertificateAuthorityServerType(command_line, &aca_type);
      if (status != EX_OK) {
        return status;
      }
      if (!command_line->HasSwitch("input")) {
        return EX_USAGE;
      }
      std::string input;
      base::FilePath filename(command_line->GetSwitchValueASCII("input"));
      if (!base::ReadFileToString(filename, &input)) {
        LOG(ERROR) << "Failed to read file: " << filename.value();
        return EX_NOINPUT;
      }
      task = base::Bind(&ClientLoop::CallFinishEnroll,
                        weak_factory_.GetWeakPtr(), aca_type, input);
    } else if (args.front() == kEnrollCommand) {
      ACAType aca_type;
      int status = GetCertificateAuthorityServerType(command_line, &aca_type);
      if (status != EX_OK) {
        return status;
      }
      bool forced = command_line->HasSwitch("forced");
      task = base::Bind(&ClientLoop::CallEnroll, weak_factory_.GetWeakPtr(),
                        aca_type, forced);
    } else if (args.front() == kCreateCertRequestCommand) {
      ACAType aca_type;
      int status = GetCertificateAuthorityServerType(command_line, &aca_type);
      if (status != EX_OK) {
        return status;
      }
      std::string profile_str = command_line->GetSwitchValueASCII("profile");
      CertificateProfile profile;
      if (profile_str.empty() || profile_str == "enterprise_user" ||
          profile_str == "user" || profile_str == "u") {
        profile = ENTERPRISE_USER_CERTIFICATE;
      } else if (profile_str == "enterprise_machine" ||
                 profile_str == "machine" || profile_str == "m") {
        profile = ENTERPRISE_MACHINE_CERTIFICATE;
      } else if (profile_str == "enterprise_enrollment" ||
                 profile_str == "enrollment" || profile_str == "e") {
        profile = ENTERPRISE_ENROLLMENT_CERTIFICATE;
      } else if (profile_str == "content_protection" ||
                 profile_str == "content" || profile_str == "c") {
        profile = CONTENT_PROTECTION_CERTIFICATE;
      } else if (profile_str == "content_protection_with_stable_id" ||
                 profile_str == "cpsi") {
        profile = CONTENT_PROTECTION_CERTIFICATE_WITH_STABLE_ID;
      } else if (profile_str == "cast") {
        profile = CAST_CERTIFICATE;
      } else if (profile_str == "gfsc") {
        profile = GFSC_CERTIFICATE;
      } else {
        return EX_USAGE;
      }
      task = base::Bind(&ClientLoop::CallCreateCertRequest,
                        weak_factory_.GetWeakPtr(), aca_type, profile,
                        command_line->GetSwitchValueASCII("user"),
                        command_line->GetSwitchValueASCII("origin"));
    } else if (args.front() == kFinishCertRequestCommand) {
      if (!command_line->HasSwitch("input")) {
        return EX_USAGE;
      }
      std::string input;
      base::FilePath filename(command_line->GetSwitchValueASCII("input"));
      if (!base::ReadFileToString(filename, &input)) {
        LOG(ERROR) << "Failed to read file: " << filename.value();
        return EX_NOINPUT;
      }
      task = base::Bind(&ClientLoop::CallFinishCertRequest,
                        weak_factory_.GetWeakPtr(), input,
                        command_line->GetSwitchValueASCII("label"),
                        command_line->GetSwitchValueASCII("user"));
    } else if (args.front() == kGetCertCommand) {
      ACAType aca_type;
      int status = GetCertificateAuthorityServerType(command_line, &aca_type);
      if (status != EX_OK) {
        return status;
      }
      std::string profile_str = command_line->GetSwitchValueASCII("profile");
      CertificateProfile profile;
      if (profile_str.empty() || profile_str == "enterprise_user" ||
          profile_str == "user" || profile_str == "u") {
        profile = ENTERPRISE_USER_CERTIFICATE;
      } else if (profile_str == "enterprise_machine" ||
                 profile_str == "machine" || profile_str == "m") {
        profile = ENTERPRISE_MACHINE_CERTIFICATE;
      } else if (profile_str == "enterprise_enrollment" ||
                 profile_str == "enrollment" || profile_str == "e") {
        profile = ENTERPRISE_ENROLLMENT_CERTIFICATE;
      } else if (profile_str == "content_protection" ||
                 profile_str == "content" || profile_str == "c") {
        profile = CONTENT_PROTECTION_CERTIFICATE;
      } else if (profile_str == "content_protection_with_stable_id" ||
                 profile_str == "cpsi") {
        profile = CONTENT_PROTECTION_CERTIFICATE_WITH_STABLE_ID;
      } else if (profile_str == "cast") {
        profile = CAST_CERTIFICATE;
      } else if (profile_str == "gfsc") {
        profile = GFSC_CERTIFICATE;
      } else {
        return EX_USAGE;
      }
      bool forced = command_line->HasSwitch("forced");
      bool shall_trigger_enrollment = command_line->HasSwitch("enroll");
      task = base::Bind(&ClientLoop::CallGetCert, weak_factory_.GetWeakPtr(),
                        aca_type, profile,
                        command_line->GetSwitchValueASCII("label"),
                        command_line->GetSwitchValueASCII("user"),
                        command_line->GetSwitchValueASCII("origin"), forced,
                        shall_trigger_enrollment);
    } else if (args.front() == kSignChallengeCommand) {
      if (!command_line->HasSwitch("input")) {
        return EX_USAGE;
      }
      std::string input;
      base::FilePath filename(command_line->GetSwitchValueASCII("input"));
      if (!base::ReadFileToString(filename, &input)) {
        LOG(ERROR) << "Failed to read file: " << filename.value();
        return EX_NOINPUT;
      }
      if (command_line->HasSwitch("enterprise")) {
        VAType va_type;
        int status = GetVerifiedAccessServerType(command_line, &va_type);
        if (status != EX_OK) {
          return status;
        }
        task = base::Bind(&ClientLoop::CallSignEnterpriseChallenge,
                          weak_factory_.GetWeakPtr(), va_type, input,
                          command_line->GetSwitchValueASCII("label"),
                          command_line->GetSwitchValueASCII("user"),
                          command_line->GetSwitchValueASCII("domain"),
                          command_line->GetSwitchValueASCII("device_id"),
                          command_line->HasSwitch("spkac"));
      } else {
        task = base::Bind(&ClientLoop::CallSignSimpleChallenge,
                          weak_factory_.GetWeakPtr(), input,
                          command_line->GetSwitchValueASCII("label"),
                          command_line->GetSwitchValueASCII("user"));
      }
    } else if (args.front() == kGetEnrollmentId) {
      task =
          base::Bind(&ClientLoop::GetEnrollmentId, weak_factory_.GetWeakPtr(),
                     command_line->HasSwitch("ignore_cache"));
    } else if (args.front() == kGetCertifiedNvIndex) {
      task = base::Bind(&ClientLoop::GetCertifiedNvIndex,
                        weak_factory_.GetWeakPtr(),
                        command_line->GetSwitchValueASCII("index"),
                        command_line->GetSwitchValueASCII("size"),
                        command_line->GetSwitchValueASCII("key_label"));
    } else {
      return EX_USAGE;
    }
    base::ThreadTaskRunnerHandle::Get()->PostTask(FROM_HERE, task);
    return EX_OK;
  }

  int GetVerifiedAccessServerType(base::CommandLine* command_line,
                                  VAType* va_type) {
    *va_type = DEFAULT_VA;
    if (command_line->HasSwitch("va-server")) {
      std::string va_server(command_line->GetSwitchValueASCII("va-server"));
      if (va_server == "test") {
        *va_type = TEST_VA;
      } else if (va_server != "" && va_server != "default") {
        LOG(ERROR) << "Invalid va-server value: " << va_server;
        return EX_USAGE;
      }
    } else {
      // Convert the CA type to a VA server type.
      ACAType aca_type;
      int status = GetCertificateAuthorityServerType(command_line, &aca_type);
      if (status != EX_OK) {
        return status;
      }
      switch (aca_type) {
        case TEST_ACA:
          *va_type = TEST_VA;
          break;

        case DEFAULT_ACA:
        default:
          *va_type = DEFAULT_VA;
          break;
      }
    }
    return EX_OK;
  }

  int GetCertificateAuthorityServerType(base::CommandLine* command_line,
                                        ACAType* aca_type) {
    *aca_type = DEFAULT_ACA;
    std::string aca_server(
        command_line->GetSwitchValueASCII("attestation-server"));
    if (aca_server == "test") {
      *aca_type = TEST_ACA;
    } else if (aca_server != "" && aca_server != "default") {
      LOG(ERROR) << "Invalid attestation-server value: " << aca_server;
      return EX_USAGE;
    }
    return EX_OK;
  }

  template <typename ProtobufType>
  void PrintReplyAndQuit(const ProtobufType& reply) {
    printf("%s\n", GetProtoDebugString(reply).c_str());
    Quit();
  }

  void WriteOutput(const std::string& output) {
    base::FilePath filename(
        base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII("output"));
    if (base::WriteFile(filename, output.data(), output.size()) !=
        static_cast<int>(output.size())) {
      LOG(ERROR) << "Failed to write file: " << filename.value();
      QuitWithExitCode(EX_IOERR);
    }
  }

  void CallGetStatus(bool extended_status) {
    GetStatusRequest request;
    request.set_extended_status(extended_status);
    attestation_->GetStatus(
        request, base::Bind(&ClientLoop::PrintReplyAndQuit<GetStatusReply>,
                            weak_factory_.GetWeakPtr()));
  }

  void CallGetKeyInfo(const std::string& label, const std::string& username) {
    GetKeyInfoRequest request;
    request.set_key_label(label);
    request.set_username(username);
    attestation_->GetKeyInfo(
        request, base::Bind(&ClientLoop::PrintReplyAndQuit<GetKeyInfoReply>,
                            weak_factory_.GetWeakPtr()));
  }

  void CallSetKeyPayload(const std::string& payload,
                         const std::string& label,
                         const std::string& username) {
    SetKeyPayloadRequest request;
    request.set_key_label(label);
    request.set_username(username);
    request.set_payload(payload);
    attestation_->SetKeyPayload(
        request, base::Bind(&ClientLoop::PrintReplyAndQuit<SetKeyPayloadReply>,
                            weak_factory_.GetWeakPtr()));
  }

  void CallDeleteKeys(const std::string& prefix,
                      const std::string& label,
                      const std::string& username) {
    DeleteKeysRequest request;
    if (!label.empty()) {
      request.set_key_label_match(label);
      request.set_match_behavior(DeleteKeysRequest::MATCH_BEHAVIOR_EXACT);
    }
    if (!prefix.empty()) {
      request.set_key_label_match(prefix);
      request.set_match_behavior(DeleteKeysRequest::MATCH_BEHAVIOR_PREFIX);
    }
    request.set_username(username);
    attestation_->DeleteKeys(
        request, base::Bind(&ClientLoop::PrintReplyAndQuit<DeleteKeysReply>,
                            weak_factory_.GetWeakPtr()));
  }

  void CallGetEndorsementInfo() {
    GetEndorsementInfoRequest request;
    attestation_->GetEndorsementInfo(
        request,
        base::Bind(&ClientLoop::PrintReplyAndQuit<GetEndorsementInfoReply>,
                   weak_factory_.GetWeakPtr()));
  }

  void CallGetAttestationKeyInfo(ACAType aca_type) {
    GetAttestationKeyInfoRequest request;
    request.set_aca_type(aca_type);
    attestation_->GetAttestationKeyInfo(
        request,
        base::Bind(&ClientLoop::PrintReplyAndQuit<GetAttestationKeyInfoReply>,
                   weak_factory_.GetWeakPtr()));
  }

  void CallVerifyAttestation(bool cros_core, bool ek_only) {
    VerifyRequest request;
    request.set_cros_core(cros_core);
    request.set_ek_only(ek_only);
    attestation_->Verify(request,
                         base::Bind(&ClientLoop::PrintReplyAndQuit<VerifyReply>,
                                    weak_factory_.GetWeakPtr()));
  }

  void CallActivateAttestationKey(ACAType aca_type,
                                  const std::string& input,
                                  bool save_certificate) {
    ActivateAttestationKeyRequest request;
    request.set_aca_type(aca_type);
    request.mutable_encrypted_certificate()->ParseFromString(input);
    request.set_save_certificate(save_certificate);
    attestation_->ActivateAttestationKey(
        request,
        base::Bind(&ClientLoop::PrintReplyAndQuit<ActivateAttestationKeyReply>,
                   weak_factory_.GetWeakPtr()));
  }

  void EncryptForActivate(const std::string& input) {
    GetEndorsementInfoRequest request;
    attestation_->GetEndorsementInfo(
        request, base::Bind(&ClientLoop::EncryptForActivate2,
                            weak_factory_.GetWeakPtr(), input));
  }

  void EncryptForActivate2(const std::string& input,
                           const GetEndorsementInfoReply& endorsement_info) {
    if (endorsement_info.status() != STATUS_SUCCESS) {
      PrintReplyAndQuit(endorsement_info);
    }
    GetAttestationKeyInfoRequest request;
    attestation_->GetAttestationKeyInfo(
        request,
        base::Bind(&ClientLoop::EncryptForActivate3, weak_factory_.GetWeakPtr(),
                   input, endorsement_info));
  }

  void EncryptForActivate3(
      const std::string& input,
      const GetEndorsementInfoReply& endorsement_info,
      const GetAttestationKeyInfoReply& attestation_key_info) {
    if (attestation_key_info.status() != STATUS_SUCCESS) {
      PrintReplyAndQuit(attestation_key_info);
    }
    CryptoUtilityImpl crypto(nullptr);
    EncryptedIdentityCredential encrypted;
#if !USE_TPM2
    TpmVersion tpm_version = TPM_1_2;
#else
    TpmVersion tpm_version = TPM_2_0;
#endif
    if (!crypto.EncryptIdentityCredential(
            tpm_version, input, endorsement_info.ek_public_key(),
            attestation_key_info.public_key_tpm_format(), &encrypted)) {
      QuitWithExitCode(EX_SOFTWARE);
    }
    std::string output;
    encrypted.SerializeToString(&output);
    WriteOutput(output);
    Quit();
  }

  void CallCreateCertifiableKey(const std::string& label,
                                const std::string& username,
                                KeyUsage usage) {
    CreateCertifiableKeyRequest request;
    request.set_key_label(label);
    request.set_username(username);
    request.set_key_type(KEY_TYPE_RSA);
    request.set_key_usage(usage);
    attestation_->CreateCertifiableKey(
        request,
        base::Bind(&ClientLoop::PrintReplyAndQuit<CreateCertifiableKeyReply>,
                   weak_factory_.GetWeakPtr()));
  }

  void Encrypt(const std::string& label,
               const std::string& username,
               const std::string& input) {
    GetKeyInfoRequest request;
    request.set_key_label(label);
    request.set_username(username);
    attestation_->GetKeyInfo(
        request,
        base::Bind(&ClientLoop::Encrypt2, weak_factory_.GetWeakPtr(), input));
  }

  void Encrypt2(const std::string& input, const GetKeyInfoReply& key_info) {
    CryptoUtilityImpl crypto(nullptr);
    std::string output;
    if (!crypto.EncryptForUnbind(key_info.public_key(), input, &output)) {
      QuitWithExitCode(EX_SOFTWARE);
    }
    WriteOutput(output);
    Quit();
  }

  void CallDecrypt(const std::string& label,
                   const std::string& username,
                   const std::string& input) {
    DecryptRequest request;
    request.set_key_label(label);
    request.set_username(username);
    request.set_encrypted_data(input);
    attestation_->Decrypt(
        request, base::Bind(&ClientLoop::PrintReplyAndQuit<DecryptReply>,
                            weak_factory_.GetWeakPtr()));
  }

  void CallSign(const std::string& label,
                const std::string& username,
                const std::string& input) {
    SignRequest request;
    request.set_key_label(label);
    request.set_username(username);
    request.set_data_to_sign(input);
    attestation_->Sign(request, base::Bind(&ClientLoop::OnSignComplete,
                                           weak_factory_.GetWeakPtr()));
  }

  void OnSignComplete(const SignReply& reply) {
    if (reply.status() == STATUS_SUCCESS &&
        base::CommandLine::ForCurrentProcess()->HasSwitch("output")) {
      WriteOutput(reply.signature());
    }
    PrintReplyAndQuit<SignReply>(reply);
  }

  void VerifySignature(const std::string& label,
                       const std::string& username,
                       const std::string& input,
                       const std::string& signature) {
    GetKeyInfoRequest request;
    request.set_key_label(label);
    request.set_username(username);
    attestation_->GetKeyInfo(
        request, base::Bind(&ClientLoop::VerifySignature2,
                            weak_factory_.GetWeakPtr(), input, signature));
  }

  void VerifySignature2(const std::string& input,
                        const std::string& signature,
                        const GetKeyInfoReply& key_info) {
    CryptoUtilityImpl crypto(nullptr);
    if (crypto.VerifySignature(crypto.DefaultDigestAlgoForSignature(),
                               key_info.public_key(), input, signature)) {
      printf("Signature is OK!\n");
    } else {
      printf("Signature is BAD!\n");
    }
    Quit();
  }

  void CallRegister(const std::string& label, const std::string& username) {
    RegisterKeyWithChapsTokenRequest request;
    request.set_key_label(label);
    request.set_username(username);
    attestation_->RegisterKeyWithChapsToken(
        request,
        base::Bind(
            &ClientLoop::PrintReplyAndQuit<RegisterKeyWithChapsTokenReply>,
            weak_factory_.GetWeakPtr()));
  }

  void CallCreateEnrollRequest(ACAType aca_type) {
    CreateEnrollRequestRequest request;
    request.set_aca_type(aca_type);
    attestation_->CreateEnrollRequest(
        request, base::Bind(&ClientLoop::OnCreateEnrollRequestComplete,
                            weak_factory_.GetWeakPtr()));
  }

  void OnCreateEnrollRequestComplete(const CreateEnrollRequestReply& reply) {
    if (reply.status() == STATUS_SUCCESS &&
        base::CommandLine::ForCurrentProcess()->HasSwitch("output")) {
      WriteOutput(reply.pca_request());
    }
    PrintReplyAndQuit<CreateEnrollRequestReply>(reply);
  }

  void CallFinishEnroll(ACAType aca_type, const std::string& pca_response) {
    FinishEnrollRequest request;
    request.set_aca_type(aca_type);
    request.set_pca_response(pca_response);
    attestation_->FinishEnroll(
        request, base::Bind(&ClientLoop::PrintReplyAndQuit<FinishEnrollReply>,
                            weak_factory_.GetWeakPtr()));
  }

  void CallEnroll(ACAType aca_type, bool forced) {
    EnrollRequest request;
    request.set_aca_type(aca_type);
    request.set_forced(forced);
    attestation_->Enroll(request,
                         base::Bind(&ClientLoop::PrintReplyAndQuit<EnrollReply>,
                                    weak_factory_.GetWeakPtr()));
  }

  void CallCreateCertRequest(ACAType aca_type,
                             CertificateProfile profile,
                             const std::string& username,
                             const std::string& origin) {
    CreateCertificateRequestRequest request;
    request.set_aca_type(aca_type);
    request.set_certificate_profile(profile);
    request.set_username(username);
    request.set_request_origin(origin);
    attestation_->CreateCertificateRequest(
        request, base::Bind(&ClientLoop::OnCreateCertRequestComplete,
                            weak_factory_.GetWeakPtr()));
  }

  void OnCreateCertRequestComplete(const CreateCertificateRequestReply& reply) {
    if (reply.status() == STATUS_SUCCESS &&
        base::CommandLine::ForCurrentProcess()->HasSwitch("output")) {
      WriteOutput(reply.pca_request());
    }
    PrintReplyAndQuit<CreateCertificateRequestReply>(reply);
  }

  void CallFinishCertRequest(const std::string& pca_response,
                             const std::string& label,
                             const std::string& username) {
    FinishCertificateRequestRequest request;
    request.set_pca_response(pca_response);
    request.set_key_label(label);
    request.set_username(username);
    attestation_->FinishCertificateRequest(
        request,
        base::Bind(
            &ClientLoop::PrintReplyAndQuit<FinishCertificateRequestReply>,
            weak_factory_.GetWeakPtr()));
  }

  void CallGetCert(ACAType aca_type,
                   CertificateProfile profile,
                   const std::string& label,
                   const std::string& username,
                   const std::string& origin,
                   bool forced,
                   bool shall_trigger_enrollment) {
    GetCertificateRequest request;
    request.set_aca_type(aca_type);
    request.set_certificate_profile(profile);
    request.set_key_label(label);
    request.set_username(username);
    request.set_request_origin(origin);
    request.set_forced(forced);
    request.set_shall_trigger_enrollment(shall_trigger_enrollment);
    attestation_->GetCertificate(
        request, base::Bind(&ClientLoop::PrintReplyAndQuit<GetCertificateReply>,
                            weak_factory_.GetWeakPtr()));
  }

  void CallSignEnterpriseChallenge(VAType va_type,
                                   const std::string& input,
                                   const std::string& label,
                                   const std::string& username,
                                   const std::string& domain,
                                   const std::string& device_id,
                                   bool include_spkac) {
    SignEnterpriseChallengeRequest request;
    request.set_va_type(va_type);
    request.set_key_label(label);
    request.set_username(username);
    request.set_domain(domain);
    request.set_device_id(device_id);
    request.set_include_signed_public_key(include_spkac);
    request.set_challenge(input);
    attestation_->SignEnterpriseChallenge(
        request, base::Bind(&ClientLoop::OnSignEnterpriseChallengeComplete,
                            weak_factory_.GetWeakPtr()));
  }

  void OnSignEnterpriseChallengeComplete(
      const SignEnterpriseChallengeReply& reply) {
    if (reply.status() == STATUS_SUCCESS &&
        base::CommandLine::ForCurrentProcess()->HasSwitch("output")) {
      WriteOutput(reply.challenge_response());
    }
    PrintReplyAndQuit<SignEnterpriseChallengeReply>(reply);
  }

  void CallSignSimpleChallenge(const std::string& input,
                               const std::string& label,
                               const std::string& username) {
    SignSimpleChallengeRequest request;
    request.set_key_label(label);
    request.set_username(username);
    request.set_challenge(input);
    attestation_->SignSimpleChallenge(
        request, base::Bind(&ClientLoop::OnSignSimpleChallengeComplete,
                            weak_factory_.GetWeakPtr()));
  }

  void OnSignSimpleChallengeComplete(const SignSimpleChallengeReply& reply) {
    if (reply.status() == STATUS_SUCCESS &&
        base::CommandLine::ForCurrentProcess()->HasSwitch("output")) {
      WriteOutput(reply.challenge_response());
    }
    PrintReplyAndQuit<SignSimpleChallengeReply>(reply);
  }

  void GetEnrollmentId(bool ignore_cache) {
    GetEnrollmentIdRequest request;
    request.set_ignore_cache(ignore_cache);
    attestation_->GetEnrollmentId(
        request, base::Bind(&ClientLoop::OnGetEnrollmentIdComplete,
                            weak_factory_.GetWeakPtr()));
  }

  void OnGetEnrollmentIdComplete(const GetEnrollmentIdReply& reply) {
    PrintReplyAndQuit<GetEnrollmentIdReply>(reply);
  }

  void GetCertifiedNvIndex(const std::string& index,
                           const std::string& size_bytes,
                           const std::string& key_label) {
    GetCertifiedNvIndexRequest request;
    uint32_t parsed_index;
    uint32_t parsed_size;

    if (!base::HexStringToUInt(index, &parsed_index))
      LOG(ERROR) << "Failed to parse index.";
    if (!base::StringToUint(size_bytes, &parsed_size))
      LOG(ERROR) << "Failed to parse size.";

    request.set_nv_index(parsed_index);
    request.set_nv_size(parsed_size);
    request.set_key_label(key_label);

    attestation_->GetCertifiedNvIndex(
        request, base::Bind(&ClientLoop::OnGetCertifiedNvIndexComplete,
                            weak_factory_.GetWeakPtr()));
  }

  void OnGetCertifiedNvIndexComplete(const GetCertifiedNvIndexReply& reply) {
    if (reply.status() == STATUS_SUCCESS &&
        base::CommandLine::ForCurrentProcess()->HasSwitch("output")) {
      WriteOutput(reply.SerializeAsString());
    }
    PrintReplyAndQuit<GetCertifiedNvIndexReply>(reply);
  }

  std::unique_ptr<attestation::AttestationInterface> attestation_;

  // Declare this last so weak pointers will be destroyed first.
  base::WeakPtrFactory<ClientLoop> weak_factory_{this};

  DISALLOW_COPY_AND_ASSIGN(ClientLoop);
};

}  // namespace attestation

int main(int argc, char* argv[]) {
  base::CommandLine::Init(argc, argv);
  brillo::InitLog(brillo::kLogToStderr);
  attestation::ClientLoop loop;
  return loop.Run();
}
