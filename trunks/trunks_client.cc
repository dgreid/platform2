// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// trunks_client is a command line tool that supports various TPM operations. It
// does not provide direct access to the trunksd D-Bus interface.

#include <inttypes.h>
#include <stdio.h>

#include <memory>
#include <string>

#include <base/command_line.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/timer/elapsed_timer.h>
#include <brillo/file_utils.h>
#include <brillo/syslog_logging.h>
#include <crypto/libcrypto-compat.h>
#include <crypto/scoped_openssl_types.h>

#include "trunks/error_codes.h"
#include "trunks/hmac_session.h"
#include "trunks/password_authorization_delegate.h"
#include "trunks/policy_session.h"
#include "trunks/scoped_key_handle.h"
#include "trunks/session_manager.h"
#include "trunks/tpm_state.h"
#include "trunks/tpm_utility.h"
#include "trunks/trunks_client_test.h"
#include "trunks/trunks_factory_impl.h"

namespace {

using trunks::CommandTransceiver;
using trunks::TrunksFactory;
using trunks::TrunksFactoryImpl;

void PrintUsage() {
  puts("Options:");
  puts("  --allocate_pcr - Configures PCR 0-15 under the SHA256 bank.");
  puts("  --clear - Clears the TPM. Use before initializing the TPM.");
  puts("  --help - Prints this message.");
  puts("  --init_tpm - Initializes a TPM as CrOS firmware does.");
  puts("  --own - Takes ownership of the TPM with the provided password.");
  puts("  --owner_password - used to provide an owner password");
  puts("  --endorsement_password - used to provide an endorsement password");
  puts("  --regression_test - Runs some basic regression tests. If");
  puts("                      *_password is supplied, it runs tests that");
  puts("                      require the permissions.");
  puts("  --startup - Performs startup and self-tests.");
  puts("  --status - Prints TPM status information.");
  puts("  --stress_test - Runs some basic stress tests.");
  puts("  --read_pcr --index=<N> - Reads a PCR and prints the value.");
  puts("  --extend_pcr --index=<N> --value=<value> - Extends a PCR.");
  puts("  --tpm_version - Prints TPM versions and IDs similar to tpm_version.");
  puts("  --endorsement_public_key - Prints the public endorsement key.");
  puts("  --key_create (--rsa=<bits>|--ecc) --usage=sign|decrypt|all");
  puts("               --key_blob=<file> [--print_time] [--sess_*]");
  puts("                    - Creates a key and saves the blob to file.");
  puts("  --key_load --key_blob=<file> [--print_time] [--sess_*]");
  puts("                    - Loads key from blob, returns handle.");
  puts("  --key_sign --handle=<H> --data=<in_file> --signature=<out_file>");
  puts("             [--ecc] [--print_time] [--sess_*]");
  puts("                    - Signs the hash of data using the loaded key.");
  puts("  --key_info --handle=<H> - Prints information about the loaded key.");
  puts("  --key_test_short_ecc --handle=<H>.");
  puts("  --sess_* - group of options providing parameters for auth session:");
  puts("      --sess_salted");
  puts("      --sess_encrypted");
  puts("  --ext_command_test - Runs regression tests on extended commands.");
}

std::string HexEncode(const std::string& bytes) {
  return base::HexEncode(bytes.data(), bytes.size());
}

std::string HexEncode(const trunks::TPM2B_DIGEST& tpm2b) {
  return base::HexEncode(tpm2b.buffer, tpm2b.size);
}
std::string HexEncode(const trunks::TPM2B_ECC_PARAMETER& tpm2b) {
  return base::HexEncode(tpm2b.buffer, tpm2b.size);
}
std::string HexEncode(const trunks::TPM2B_PUBLIC_KEY_RSA& tpm2b) {
  return base::HexEncode(tpm2b.buffer, tpm2b.size);
}

int OutputToFile(const std::string& file_name, const std::string& data) {
  if (!brillo::WriteStringToFile(base::FilePath(file_name), data)) {
    LOG(ERROR) << "Failed to write to " << file_name;
    return -1;
  }
  return 0;
}

int InputFromFile(const std::string& file_name, std::string* data) {
  if (!base::ReadFileToString(base::FilePath(file_name), data)) {
    LOG(ERROR) << "Failed to write to " << file_name;
    return -1;
  }
  return 0;
}

template <typename OP, typename... ARGS>
trunks::TPM_RC CallTimed(bool print_time,
                         const char* op_name,
                         OP op_func,
                         ARGS&&... args) {
  base::ElapsedTimer timer;
  trunks::TPM_RC rc = op_func(args...);
  if (print_time) {
    printf("%s took %" PRId64 " ms\n", op_name,
           timer.Elapsed().InMilliseconds());
  }
  return rc;
}

template <typename OP, typename... ARGS>
trunks::TPM_RC CallTpmUtility(bool print_time,
                              const TrunksFactory& factory,
                              const char* op_name,
                              OP op_func,
                              ARGS&&... args) {
  std::unique_ptr<trunks::TpmUtility> tpm_utility = factory.GetTpmUtility();
  trunks::TPM_RC rc = CallTimed(print_time, op_name, std::mem_fn(op_func),
                                tpm_utility.get(), args...);
  LOG_IF(ERROR, rc) << "Error during " << op_name << ": "
                    << trunks::GetErrorString(rc);
  return rc;
}

int Startup(const TrunksFactory& factory) {
  factory.GetTpmUtility()->Shutdown();
  return factory.GetTpmUtility()->Startup();
}

int Clear(const TrunksFactory& factory) {
  return factory.GetTpmUtility()->Clear();
}

int InitializeTpm(const TrunksFactory& factory) {
  return factory.GetTpmUtility()->InitializeTpm();
}

int AllocatePCR(const TrunksFactory& factory) {
  trunks::TPM_RC result;
  result = factory.GetTpmUtility()->AllocatePCR("");
  if (result != trunks::TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error allocating PCR:" << trunks::GetErrorString(result);
    return result;
  }
  factory.GetTpmUtility()->Shutdown();
  return factory.GetTpmUtility()->Startup();
}

int TakeOwnership(const std::string& owner_password,
                  const TrunksFactory& factory) {
  trunks::TPM_RC rc;
  rc = factory.GetTpmUtility()->TakeOwnership(owner_password, owner_password,
                                              owner_password);
  if (rc) {
    LOG(ERROR) << "Error taking ownership: " << trunks::GetErrorString(rc);
    return rc;
  }
  return 0;
}

int DumpStatus(const TrunksFactory& factory) {
  std::unique_ptr<trunks::TpmState> state = factory.GetTpmState();
  trunks::TPM_RC result = state->Initialize();
  if (result != trunks::TPM_RC_SUCCESS) {
    LOG(ERROR) << "Failed to read TPM state: "
               << trunks::GetErrorString(result);
    return result;
  }
  printf("Owner password set: %s\n",
         state->IsOwnerPasswordSet() ? "true" : "false");
  printf("Endorsement password set: %s\n",
         state->IsEndorsementPasswordSet() ? "true" : "false");
  printf("Lockout password set: %s\n",
         state->IsLockoutPasswordSet() ? "true" : "false");
  printf("Ownership status: %s\n", state->IsOwned() ? "true" : "false");
  printf("In lockout: %s\n", state->IsInLockout() ? "true" : "false");
  printf("Platform hierarchy enabled: %s\n",
         state->IsPlatformHierarchyEnabled() ? "true" : "false");
  printf("Storage hierarchy enabled: %s\n",
         state->IsStorageHierarchyEnabled() ? "true" : "false");
  printf("Endorsement hierarchy enabled: %s\n",
         state->IsEndorsementHierarchyEnabled() ? "true" : "false");
  printf("Is Tpm enabled: %s\n", state->IsEnabled() ? "true" : "false");
  printf("Was shutdown orderly: %s\n",
         state->WasShutdownOrderly() ? "true" : "false");
  printf("Is RSA supported: %s\n", state->IsRSASupported() ? "true" : "false");
  printf("Is ECC supported: %s\n", state->IsECCSupported() ? "true" : "false");
  printf("Lockout Counter: %u\n", state->GetLockoutCounter());
  printf("Lockout Threshold: %u\n", state->GetLockoutThreshold());
  printf("Lockout Interval: %u\n", state->GetLockoutInterval());
  printf("Lockout Recovery: %u\n", state->GetLockoutRecovery());
  return 0;
}

int ReadPCR(const TrunksFactory& factory, int index) {
  std::unique_ptr<trunks::TpmUtility> tpm_utility = factory.GetTpmUtility();
  std::string value;
  trunks::TPM_RC result = tpm_utility->ReadPCR(index, &value);
  if (result) {
    LOG(ERROR) << "ReadPCR: " << trunks::GetErrorString(result);
    return result;
  }
  printf("PCR Value: %s\n", HexEncode(value).c_str());
  return 0;
}

int ExtendPCR(const TrunksFactory& factory,
              int index,
              const std::string& value) {
  std::unique_ptr<trunks::TpmUtility> tpm_utility = factory.GetTpmUtility();
  trunks::TPM_RC result = tpm_utility->ExtendPCR(index, value, nullptr);
  if (result) {
    LOG(ERROR) << "ExtendPCR: " << trunks::GetErrorString(result);
    return result;
  }
  return 0;
}

char* TpmPropertyToStr(uint32_t value) {
  static char str[5];
  char c;
  int i = 0;
  int shift = 24;
  for (; i < 4; i++, shift -= 8) {
    c = static_cast<char>((value >> shift) & 0xFF);
    if (c == 0)
      break;
    str[i] = (c >= 32 && c < 127) ? c : ' ';
  }
  str[i] = 0;
  return str;
}

int TpmVersion(const TrunksFactory& factory) {
  std::unique_ptr<trunks::TpmState> state = factory.GetTpmState();
  trunks::TPM_RC result = state->Initialize();
  if (result != trunks::TPM_RC_SUCCESS) {
    LOG(ERROR) << "Failed to read TPM state: "
               << trunks::GetErrorString(result);
    return result;
  }
  printf("  TPM 2.0 Version Info:\n");
  // Print Chip Version for compatibility with tpm_version, hardcoded as
  // there's no 2.0 equivalent (TPM_PT_FAMILY_INDICATOR is const).
  printf("  Chip Version:        2.0.0.0\n");
  uint32_t family = state->GetTpmFamily();
  printf("  Spec Family:         %08" PRIx32 "\n", family);
  printf("  Spec Family String:  %s\n", TpmPropertyToStr(family));
  printf("  Spec Level:          %" PRIu32 "\n",
         state->GetSpecificationLevel());
  printf("  Spec Revision:       %" PRIu32 "\n",
         state->GetSpecificationRevision());
  uint32_t manufacturer = state->GetManufacturer();
  printf("  Manufacturer Info:   %08" PRIx32 "\n", manufacturer);
  printf("  Manufacturer String: %s\n", TpmPropertyToStr(manufacturer));
  printf("  Vendor ID:           %s\n", state->GetVendorIDString().c_str());
  printf("  TPM Model:           %08" PRIx32 "\n", state->GetTpmModel());
  printf("  Firmware Version:    %016" PRIx64 "\n",
         state->GetFirmwareVersion());

  return 0;
}

int EndorsementPublicKey(const TrunksFactory& factory) {
  std::string ekm;
  factory.GetTpmUtility()->GetPublicRSAEndorsementKeyModulus(&ekm);
  std::string ekm_hex = HexEncode(ekm);
  printf("  Public Endorsement Key Modulus: %s\n", ekm_hex.c_str());
  return 0;
}

int KeyStartSession(trunks::SessionManager* session_manager,
                    base::CommandLine* cl,
                    trunks::HmacAuthorizationDelegate* delegate) {
  bool salted = cl->HasSwitch("sess_salted");
  bool encrypted = cl->HasSwitch("sess_encrypted");
  bool print_time = cl->HasSwitch("print_time");

  trunks::TPM_RC rc =
      CallTimed(print_time, "StartSession",
                std::mem_fn(&trunks::SessionManager::StartSession),
                session_manager, trunks::TPM_SE_HMAC, trunks::TPM_RH_NULL, "",
                salted, encrypted, delegate);
  LOG_IF(ERROR, rc) << "Failed to start session: "
                    << trunks::GetErrorString(rc);
  return rc;
}

int GetKeyUsage(const std::string& option_value,
                trunks::TpmUtility::AsymmetricKeyUsage* key_usage) {
  const std::map<std::string, trunks::TpmUtility::AsymmetricKeyUsage> mapping =
      // NOLINTNEXTLINE(whitespace/braces)
      {
          {"decrypt", trunks::TpmUtility::kDecryptKey},
          {"sign", trunks::TpmUtility::kSignKey},
          {"all", trunks::TpmUtility::kDecryptAndSignKey},
      };
  auto entry = mapping.find(option_value);
  if (entry == mapping.end()) {
    LOG(ERROR) << "Unrecognized key usage: " << option_value;
    return -1;
  }
  *key_usage = entry->second;
  return 0;
}

int KeyInfo(bool print_time, const TrunksFactory& factory, uint32_t handle) {
  trunks::TPMT_PUBLIC public_area;
  if (CallTpmUtility(print_time, factory, "GetKeyPublicArea",
                     &trunks::TpmUtility::GetKeyPublicArea, handle,
                     &public_area)) {
    return -1;
  }
  puts("Key public area:");
  printf("  type: %#x\n", public_area.type);
  printf("  name_alg: %#x\n", public_area.name_alg);
  printf("  attributes: %#x\n", public_area.object_attributes);
  printf("  auth_policy: %s\n", HexEncode(public_area.auth_policy).c_str());
  if (public_area.type == trunks::TPM_ALG_RSA) {
    printf("  RSA modulus: %s\n", HexEncode(public_area.unique.rsa).c_str());
  } else if (public_area.type == trunks::TPM_ALG_ECC) {
    printf("  ECC X: %s\n", HexEncode(public_area.unique.ecc.x).c_str());
    printf("  ECC Y: %s\n", HexEncode(public_area.unique.ecc.y).c_str());
  }

  std::string key_name;
  if (CallTpmUtility(print_time, factory, "GetKeyName",
                     &trunks::TpmUtility::GetKeyName, handle, &key_name)) {
    return -1;
  }
  printf("Key name: %s\n", HexEncode(key_name).c_str());

  return 0;
}

void PrintEccPoint(const char* name, const trunks::TPM2B_ECC_POINT& point) {
  printf("%s point: [%u]", name, point.size);
  printf("  X=[%u] %s, ", point.point.x.size, HexEncode(point.point.x).c_str());
  printf("  Y=[%u] %s\n", point.point.y.size, HexEncode(point.point.y).c_str());
}

int KeyTestShortEcc(const TrunksFactory& factory, uint32_t handle) {
  std::unique_ptr<trunks::TpmUtility> tpm_utility = factory.GetTpmUtility();
  std::string name;
  trunks::TPM_RC rc = tpm_utility->GetKeyName(handle, &name);
  if (rc != trunks::TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error during GetKeyName: " << trunks::GetErrorString(rc);
    return -1;
  }

  trunks::HmacAuthorizationDelegate delegate;
  std::unique_ptr<trunks::SessionManager> session_manager =
      factory.GetSessionManager();
  rc = session_manager->StartSession(trunks::TPM_SE_HMAC, trunks::TPM_RH_NULL,
                                     "", false, false, &delegate);
  if (rc != trunks::TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error during StartSession: " << trunks::GetErrorString(rc);
    return -1;
  }

  trunks::TPM2B_ECC_POINT z_point;
  trunks::TPM2B_ECC_POINT pub_point;

  do {
    rc = factory.GetTpm()->ECDH_KeyGenSync(handle, name, &z_point, &pub_point,
                                           nullptr);
    if (rc != trunks::TPM_RC_SUCCESS) {
      LOG(ERROR) << "Error during ECDH_KeyGen: " << trunks::GetErrorString(rc);
      return -1;
    }
  } while (pub_point.point.x.buffer[0] && pub_point.point.y.buffer[0]);
  PrintEccPoint("z", z_point);
  PrintEccPoint("pub", pub_point);

  trunks::TPM2B_ECC_POINT out1_point;
  rc = factory.GetTpm()->ECDH_ZGenSync(handle, name, pub_point, &out1_point,
                                       &delegate);
  if (rc != trunks::TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error during ECDH_ZGen (pass 1): "
               << trunks::GetErrorString(rc);
    return -1;
  }
  PrintEccPoint("out1", out1_point);

  if (pub_point.point.x.buffer[0] == 0) {
    pub_point.point.x.size--;
    memmove(pub_point.point.x.buffer, pub_point.point.x.buffer + 1,
            pub_point.point.x.size);
  }
  if (pub_point.point.y.buffer[0] == 0) {
    pub_point.point.y.size--;
    memmove(pub_point.point.y.buffer, pub_point.point.y.buffer + 1,
            pub_point.point.y.size);
  }
  PrintEccPoint("shortened pub", pub_point);

  trunks::TPM2B_ECC_POINT out2_point;
  rc = factory.GetTpm()->ECDH_ZGenSync(handle, name, pub_point, &out2_point,
                                       &delegate);
  if (rc != trunks::TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error during ECDH_ZGen (pass 2): "
               << trunks::GetErrorString(rc);
    return -1;
  }
  PrintEccPoint("out2", out2_point);

  if (out1_point.point.x.size != out2_point.point.x.size ||
      out1_point.point.y.size != out2_point.point.y.size ||
      memcmp(out1_point.point.x.buffer, out2_point.point.x.buffer,
             out1_point.point.x.size) ||
      memcmp(out1_point.point.y.buffer, out2_point.point.y.buffer,
             out1_point.point.y.size)) {
    LOG(ERROR) << "Different out points produced by pass 1 and pass 2";
    return -1;
  }

  printf("SUCCESS\n");
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  base::CommandLine::Init(argc, argv);
  brillo::InitLog(brillo::kLogToStderr);
  base::CommandLine* cl = base::CommandLine::ForCurrentProcess();
  if (cl->HasSwitch("help")) {
    puts("Trunks Client: A command line tool to access the TPM.");
    PrintUsage();
    return 0;
  }

  TrunksFactoryImpl factory;
  CHECK(factory.Initialize()) << "Failed to initialize trunks factory.";

  bool print_time = cl->HasSwitch("print_time");
  if (cl->HasSwitch("status")) {
    return DumpStatus(factory);
  }
  if (cl->HasSwitch("startup")) {
    return Startup(factory);
  }
  if (cl->HasSwitch("clear")) {
    return Clear(factory);
  }
  if (cl->HasSwitch("init_tpm")) {
    return InitializeTpm(factory);
  }
  if (cl->HasSwitch("allocate_pcr")) {
    return AllocatePCR(factory);
  }

  if (cl->HasSwitch("own")) {
    return TakeOwnership(cl->GetSwitchValueASCII("owner_password"), factory);
  }
  if (cl->HasSwitch("regression_test")) {
    trunks::TrunksClientTest test(factory);
    LOG(INFO) << "Running RNG test.";
    if (!test.RNGTest()) {
      LOG(ERROR) << "Error running RNGtest.";
      return -1;
    }
    LOG(INFO) << "Running RSA key tests.";
    if (!test.SignTest()) {
      LOG(ERROR) << "Error running SignTest.";
      return -1;
    }
    if (!test.DecryptTest()) {
      LOG(ERROR) << "Error running DecryptTest.";
      return -1;
    }
    if (!test.ImportTest()) {
      LOG(ERROR) << "Error running ImportTest.";
      return -1;
    }
    if (!test.AuthChangeTest()) {
      LOG(ERROR) << "Error running AuthChangeTest.";
      return -1;
    }
    if (!test.VerifyKeyCreationTest()) {
      LOG(ERROR) << "Error running VerifyKeyCreationTest.";
      return -1;
    }
    LOG(INFO) << "Running Sealed Data test.";
    if (!test.SealedDataTest()) {
      LOG(ERROR) << "Error running SealedDataTest.";
      return -1;
    }
    LOG(INFO) << "Running Sealed to Multiple PCR Data test.";
    if (!test.SealedToMultiplePCRDataTest()) {
      LOG(ERROR) << "Error running SealedToMultiplePCRDataTest.";
      return -1;
    }
    LOG(INFO) << "Running PCR test.";
    if (!test.PCRTest()) {
      LOG(ERROR) << "Error running PCRTest.";
      return -1;
    }
    LOG(INFO) << "Running policy tests.";
    if (!test.PolicyAuthValueTest()) {
      LOG(ERROR) << "Error running PolicyAuthValueTest.";
      return -1;
    }
    if (!test.PolicyAndTest()) {
      LOG(ERROR) << "Error running PolicyAndTest.";
      return -1;
    }
    if (!test.PolicyOrTest()) {
      LOG(ERROR) << "Error running PolicyOrTest.";
      return -1;
    }
    LOG(INFO) << "Running identity key test.";
    if (!test.IdentityKeyTest()) {
      LOG(ERROR) << "Error running IdentityKeyTest.";
      return -1;
    }
    if (cl->HasSwitch("owner_password")) {
      std::string owner_password = cl->GetSwitchValueASCII("owner_password");
      LOG(INFO) << "Running NVRAM test.";
      if (!test.NvramTest(owner_password)) {
        LOG(ERROR) << "Error running NvramTest.";
        return -1;
      }
      if (cl->HasSwitch("endorsement_password")) {
        std::string endorsement_password =
            cl->GetSwitchValueASCII("endorsement_password");
        LOG(INFO) << "Running endorsement test.";
        if (!test.EndorsementTest(endorsement_password, owner_password)) {
          LOG(ERROR) << "Error running EndorsementTest.";
          return -1;
        }
      }
    }
    LOG(INFO) << "All tests were run successfully.";
    return 0;
  }
  if (cl->HasSwitch("ext_command_test")) {
    trunks::TrunksClientTest test(factory);
    LOG(INFO) << "Running PolicyFidoSigned test.";
    if (!test.PolicyFidoSignedTest(trunks::TPM_ALG_RSASSA)) {
      LOG(ERROR) << "Error running PolicyFidoSigned with RSASSA scheme.";
      return -1;
    }
    if (!test.PolicyFidoSignedTest(trunks::TPM_ALG_ECDSA)) {
      LOG(ERROR) << "Error running PolicyFidoSigned with ECDSA scheme.";
      return -1;
    }
    LOG(INFO) << "All tests were run successfully.";
    return 0;
  }
  if (cl->HasSwitch("stress_test")) {
    LOG(INFO) << "Running stress tests.";
    trunks::TrunksClientTest test(factory);
    if (!test.ManyKeysTest()) {
      LOG(ERROR) << "Error running ManyKeysTest.";
      return -1;
    }
    if (!test.ManySessionsTest()) {
      LOG(ERROR) << "Error running ManySessionsTest.";
      return -1;
    }
    return 0;
  }
  if (cl->HasSwitch("read_pcr") && cl->HasSwitch("index")) {
    return ReadPCR(factory, atoi(cl->GetSwitchValueASCII("index").c_str()));
  }
  if (cl->HasSwitch("extend_pcr") && cl->HasSwitch("index") &&
      cl->HasSwitch("value")) {
    return ExtendPCR(factory, atoi(cl->GetSwitchValueASCII("index").c_str()),
                     cl->GetSwitchValueASCII("value"));
  }
  if (cl->HasSwitch("tpm_version")) {
    return TpmVersion(factory);
  }
  if (cl->HasSwitch("endorsement_public_key")) {
    return EndorsementPublicKey(factory);
  }

  if (cl->HasSwitch("key_create") &&
      (cl->HasSwitch("rsa") || cl->HasSwitch("ecc")) &&
      cl->HasSwitch("usage") && cl->HasSwitch("key_blob")) {
    trunks::TpmUtility::AsymmetricKeyUsage key_usage;
    if (GetKeyUsage(cl->GetSwitchValueASCII("usage"), &key_usage)) {
      return -1;
    }
    trunks::HmacAuthorizationDelegate delegate;
    std::unique_ptr<trunks::SessionManager> session_manager =
        factory.GetSessionManager();
    if (KeyStartSession(session_manager.get(), cl, &delegate)) {
      return -1;
    }
    std::string key_blob;

    if (cl->HasSwitch("rsa")) {
      int modulus_bits = std::stoi(cl->GetSwitchValueASCII("rsa"), nullptr, 0);
      if (CallTpmUtility(print_time, factory, "CreateRSAKeyPair",
                         &trunks::TpmUtility::CreateRSAKeyPair, key_usage,
                         modulus_bits, 0x10001 /* exponent */,
                         "" /* password */, "" /* policy_digest */,
                         false /* use_only_policy_digest */,
                         std::vector<uint32_t>() /* pcrs */, &delegate,
                         &key_blob, nullptr /* creation_blob */)) {
        return -1;
      }
    } else {
      if (CallTpmUtility(print_time, factory, "CreateECCKeyPair",
                         &trunks::TpmUtility::CreateECCKeyPair, key_usage,
                         trunks::TPM_ECC_NIST_P256 /* curve_id */,
                         "" /* password */, "" /* policy_digest */,
                         false /* use_only_policy_digest */,
                         std::vector<uint32_t>() /* pcrs */, &delegate,
                         &key_blob, nullptr /* creation_blob */)) {
        return -1;
      }
    }
    return OutputToFile(cl->GetSwitchValueASCII("key_blob"), key_blob);
  }
  if (cl->HasSwitch("key_load") && cl->HasSwitch("key_blob")) {
    std::string key_blob;
    if (InputFromFile(cl->GetSwitchValueASCII("key_blob"), &key_blob)) {
      return -1;
    }
    trunks::HmacAuthorizationDelegate delegate;
    std::unique_ptr<trunks::SessionManager> session_manager =
        factory.GetSessionManager();
    if (KeyStartSession(session_manager.get(), cl, &delegate)) {
      return -1;
    }
    trunks::TPM_HANDLE handle;
    if (CallTpmUtility(print_time, factory, "Load",
                       &trunks::TpmUtility::LoadKey, key_blob, &delegate,
                       &handle)) {
      return -1;
    }
    printf("Loaded key handle: %#x\n", handle);
    return 0;
  }
  if (cl->HasSwitch("key_sign") && cl->HasSwitch("handle") &&
      cl->HasSwitch("data") && cl->HasSwitch("signature")) {
    uint32_t handle = std::stoul(cl->GetSwitchValueASCII("handle"), nullptr, 0);
    std::string data;
    if (InputFromFile(cl->GetSwitchValueASCII("data"), &data)) {
      return -1;
    }
    trunks::HmacAuthorizationDelegate delegate;
    std::unique_ptr<trunks::SessionManager> session_manager =
        factory.GetSessionManager();
    if (KeyStartSession(session_manager.get(), cl, &delegate)) {
      return -1;
    }
    trunks::TPM_ALG_ID signature_algorithm =
        cl->HasSwitch("ecc") ? trunks::TPM_ALG_ECDSA : trunks::TPM_ALG_RSASSA;
    std::string signature;
    if (CallTpmUtility(print_time, factory, "Sign", &trunks::TpmUtility::Sign,
                       handle, signature_algorithm, trunks::TPM_ALG_SHA256,
                       data, true /* generate_hash */, &delegate, &signature)) {
      return -1;
    }

    if (signature_algorithm == trunks::TPM_ALG_ECDSA) {
      trunks::TPMT_SIGNATURE tpm_signature;
      trunks::TPM_RC result =
          trunks::Parse_TPMT_SIGNATURE(&signature, &tpm_signature, nullptr);
      if (result != trunks::TPM_RC_SUCCESS) {
        LOG(ERROR) << "Error when parsing TPM signing result.";
        return -1;
      }

      // Pack TPM structure to OpenSSL ECDSA_SIG structure.
      crypto::ScopedECDSA_SIG openssl_ecdsa(ECDSA_SIG_new());
      crypto::ScopedBIGNUM r(BN_new()), s(BN_new());
      if (!openssl_ecdsa || !r || !s) {
        LOG(ERROR) << "Failed to allocate ECDSA_SIG or its BIGNUMs.";
        return -1;
      }
      if (!BN_bin2bn(tpm_signature.signature.ecdsa.signature_r.buffer,
                     tpm_signature.signature.ecdsa.signature_r.size, r.get()) ||
          !BN_bin2bn(tpm_signature.signature.ecdsa.signature_s.buffer,
                     tpm_signature.signature.ecdsa.signature_s.size, s.get()) ||
          !ECDSA_SIG_set0(openssl_ecdsa.get(), r.release(), s.release())) {
        LOG(ERROR) << "Error when parse TPM signing result.";
        return -1;
      }

      // Dump ECDSA_SIG to DER format
      unsigned char* openssl_buffer = nullptr;
      int length = i2d_ECDSA_SIG(openssl_ecdsa.get(), &openssl_buffer);
      crypto::ScopedOpenSSLBytes scoped_buffer(openssl_buffer);

      signature = std::string(reinterpret_cast<char*>(openssl_buffer), length);
    }

    return OutputToFile(cl->GetSwitchValueASCII("signature"), signature);
  }
  if (cl->HasSwitch("key_info") && cl->HasSwitch("handle")) {
    uint32_t handle = std::stoul(cl->GetSwitchValueASCII("handle"), nullptr, 0);
    return KeyInfo(print_time, factory, handle);
  }
  if (cl->HasSwitch("key_test_short_ecc") && cl->HasSwitch("handle")) {
    uint32_t handle = std::stoul(cl->GetSwitchValueASCII("handle"), nullptr, 0);
    return KeyTestShortEcc(factory, handle);
  }

  puts("Invalid options!");
  PrintUsage();
  return -1;
}
