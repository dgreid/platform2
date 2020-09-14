// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Cryptohome client that uses the dbus client interface

#include <inttypes.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include <memory>
#include <string>
#include <vector>

#include <attestation/proto_bindings/interface.pb.h>
#include <base/command_line.h>
#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/stl_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <brillo/cryptohome.h>
#include <brillo/glib/dbus.h>
#include <brillo/secure_blob.h>
#include <brillo/syslog_logging.h>
#include <chromeos/constants/cryptohome.h>
#include <chromeos/dbus/service_constants.h>
#include <google/protobuf/message_lite.h>

#include "cryptohome/attestation.pb.h"
#include "cryptohome/crypto.h"
#include "cryptohome/cryptolib.h"
#include "cryptohome/key.pb.h"
#include "cryptohome/mount.h"
#include "cryptohome/pkcs11_init.h"
#include "cryptohome/platform.h"
#include "cryptohome/rpc.pb.h"
#include "cryptohome/signed_secret.pb.h"
#include "cryptohome/vault_keyset.pb.h"

#include "bindings/cryptohome.dbusclient.h"

using base::FilePath;
using base::StringPrintf;
using brillo::SecureBlob;
using brillo::cryptohome::home::SanitizeUserNameWithSalt;

namespace {
// Number of days that the set_current_user_old action uses when updating the
// home directory timestamp.  ~3 months should be old enough for test purposes.
const int kSetCurrentUserOldOffsetInDays = 92;

// Five minutes is enough to wait for any TPM operations, sync() calls, etc.
const int kDefaultTimeoutMs = 300000;

// We've 100 seconds to wait for TakeOwnership(), should be rather generous.
constexpr int kWaitOwnershipTimeoutInSeconds = 100;

// Poll once every 0.2s.
constexpr int kWaitOwnershipPollIntervalInMs = 200;

}  // namespace

namespace switches {
static const char kSyslogSwitch[] = "syslog";
static const char kAttestationServerSwitch[] = "attestation-server";
static struct {
  const char* name;
  const int aca_type;
} kAttestationServers[] = {{"default", attestation::DEFAULT_ACA},
                           {"test", attestation::TEST_ACA},
                           {nullptr, attestation::ACAType_ARRAYSIZE}};
static const char kVaServerSwitch[] = "va-server";
static struct {
  const char* name;
  const int va_type;
} kVaServers[] = {{"default", attestation::DEFAULT_VA},
                  {"test", attestation::TEST_VA},
                  {nullptr, attestation::VAType_ARRAYSIZE}};
static const char kWaitOwnershipTimeoutSwitch[] = "wait-ownership-timeout";
static const char kActionSwitch[] = "action";
static const char* kActions[] = {"mount_ex",
                                 "mount_guest_ex",
                                 "unmount",
                                 "is_mounted",
                                 "check_key_ex",
                                 "remove_key_ex",
                                 "get_key_data_ex",
                                 "list_keys_ex",
                                 "migrate_key_ex",
                                 "add_key_ex",
                                 "add_data_restore_key",
                                 "mass_remove_keys",
                                 "update_key_ex",
                                 "remove",
                                 "obfuscate_user",
                                 "get_system_salt",
                                 "dump_keyset",
                                 "dump_last_activity",
                                 "tpm_status",
                                 "tpm_more_status",
                                 "status",
                                 "set_current_user_old",
                                 "tpm_take_ownership",
                                 "tpm_clear_stored_password",
                                 "tpm_wait_ownership",
                                 "install_attributes_set",
                                 "install_attributes_get",
                                 "install_attributes_finalize",
                                 "install_attributes_count",
                                 "install_attributes_is_ready",
                                 "install_attributes_is_secure",
                                 "install_attributes_is_invalid",
                                 "install_attributes_is_first_install",
                                 "pkcs11_get_user_token_info",
                                 "pkcs11_get_system_token_info",
                                 "pkcs11_is_user_token_ok",
                                 "pkcs11_terminate",
                                 "tpm_verify_attestation",
                                 "tpm_verify_ek",
                                 "tpm_attestation_status",
                                 "tpm_attestation_more_status",
                                 "tpm_attestation_start_enroll",
                                 "tpm_attestation_finish_enroll",
                                 "tpm_attestation_enroll",
                                 "tpm_attestation_start_cert_request",
                                 "tpm_attestation_finish_cert_request",
                                 "tpm_attestation_get_certificate",
                                 "tpm_attestation_key_status",
                                 "tpm_attestation_register_key",
                                 "tpm_attestation_enterprise_challenge",
                                 "tpm_attestation_simple_challenge",
                                 "tpm_attestation_get_key_payload",
                                 "tpm_attestation_set_key_payload",
                                 "tpm_attestation_delete_keys",
                                 "tpm_attestation_delete_key",
                                 "tpm_attestation_get_ek",
                                 "tpm_attestation_reset_identity",
                                 "tpm_attestation_reset_identity_result",
                                 "sign_lockbox",
                                 "verify_lockbox",
                                 "finalize_lockbox",
                                 "get_boot_attribute",
                                 "set_boot_attribute",
                                 "flush_and_sign_boot_attributes",
                                 "get_login_status",
                                 "initialize_cast_key",
                                 "get_firmware_management_parameters",
                                 "set_firmware_management_parameters",
                                 "remove_firmware_management_parameters",
                                 "migrate_to_dircrypto",
                                 "needs_dircrypto_migration",
                                 "get_enrollment_id",
                                 "get_supported_key_policies",
                                 "get_account_disk_usage",
                                 "lock_to_single_user_mount_until_reboot",
                                 "get_rsu_device_id",
                                 "check_health",
                                 "start_fingerprint_auth_session",
                                 "end_fingerprint_auth_session",
                                 NULL};
enum ActionEnum {
  ACTION_MOUNT_EX,
  ACTION_MOUNT_GUEST_EX,
  ACTION_UNMOUNT,
  ACTION_MOUNTED,
  ACTION_CHECK_KEY_EX,
  ACTION_REMOVE_KEY_EX,
  ACTION_GET_KEY_DATA_EX,
  ACTION_LIST_KEYS_EX,
  ACTION_MIGRATE_KEY_EX,
  ACTION_ADD_KEY_EX,
  ACTION_ADD_DATA_RESTORE_KEY,
  ACTION_MASS_REMOVE_KEYS,
  ACTION_UPDATE_KEY_EX,
  ACTION_REMOVE,
  ACTION_OBFUSCATE_USER,
  ACTION_GET_SYSTEM_SALT,
  ACTION_DUMP_KEYSET,
  ACTION_DUMP_LAST_ACTIVITY,
  ACTION_TPM_STATUS,
  ACTION_TPM_MORE_STATUS,
  ACTION_STATUS,
  ACTION_SET_CURRENT_USER_OLD,
  ACTION_TPM_TAKE_OWNERSHIP,
  ACTION_TPM_CLEAR_STORED_PASSWORD,
  ACTION_TPM_WAIT_OWNERSHIP,
  ACTION_INSTALL_ATTRIBUTES_SET,
  ACTION_INSTALL_ATTRIBUTES_GET,
  ACTION_INSTALL_ATTRIBUTES_FINALIZE,
  ACTION_INSTALL_ATTRIBUTES_COUNT,
  ACTION_INSTALL_ATTRIBUTES_IS_READY,
  ACTION_INSTALL_ATTRIBUTES_IS_SECURE,
  ACTION_INSTALL_ATTRIBUTES_IS_INVALID,
  ACTION_INSTALL_ATTRIBUTES_IS_FIRST_INSTALL,
  ACTION_PKCS11_GET_USER_TOKEN_INFO,
  ACTION_PKCS11_GET_SYSTEM_TOKEN_INFO,
  ACTION_PKCS11_IS_USER_TOKEN_OK,
  ACTION_PKCS11_TERMINATE,
  ACTION_TPM_VERIFY_ATTESTATION,
  ACTION_TPM_VERIFY_EK,
  ACTION_TPM_ATTESTATION_STATUS,
  ACTION_TPM_ATTESTATION_MORE_STATUS,
  ACTION_TPM_ATTESTATION_START_ENROLL,
  ACTION_TPM_ATTESTATION_FINISH_ENROLL,
  ACTION_TPM_ATTESTATION_ENROLL,
  ACTION_TPM_ATTESTATION_START_CERTREQ,
  ACTION_TPM_ATTESTATION_FINISH_CERTREQ,
  ACTION_TPM_ATTESTATION_GET_CERTIFICATE,
  ACTION_TPM_ATTESTATION_KEY_STATUS,
  ACTION_TPM_ATTESTATION_REGISTER_KEY,
  ACTION_TPM_ATTESTATION_ENTERPRISE_CHALLENGE,
  ACTION_TPM_ATTESTATION_SIMPLE_CHALLENGE,
  ACTION_TPM_ATTESTATION_GET_KEY_PAYLOAD,
  ACTION_TPM_ATTESTATION_SET_KEY_PAYLOAD,
  ACTION_TPM_ATTESTATION_DELETE_KEYS,
  ACTION_TPM_ATTESTATION_DELETE_KEY,
  ACTION_TPM_ATTESTATION_GET_EK,
  ACTION_TPM_ATTESTATION_RESET_IDENTITY,
  ACTION_TPM_ATTESTATION_RESET_IDENTITY_RESULT,
  ACTION_SIGN_LOCKBOX,
  ACTION_VERIFY_LOCKBOX,
  ACTION_FINALIZE_LOCKBOX,
  ACTION_GET_BOOT_ATTRIBUTE,
  ACTION_SET_BOOT_ATTRIBUTE,
  ACTION_FLUSH_AND_SIGN_BOOT_ATTRIBUTES,
  ACTION_GET_LOGIN_STATUS,
  ACTION_INITIALIZE_CAST_KEY,
  ACTION_GET_FIRMWARE_MANAGEMENT_PARAMETERS,
  ACTION_SET_FIRMWARE_MANAGEMENT_PARAMETERS,
  ACTION_REMOVE_FIRMWARE_MANAGEMENT_PARAMETERS,
  ACTION_MIGRATE_TO_DIRCRYPTO,
  ACTION_NEEDS_DIRCRYPTO_MIGRATION,
  ACTION_GET_ENROLLMENT_ID,
  ACTION_GET_SUPPORTED_KEY_POLICIES,
  ACTION_GET_ACCOUNT_DISK_USAGE,
  ACTION_LOCK_TO_SINGLE_USER_MOUNT_UNTIL_REBOOT,
  ACTION_GET_RSU_DEVICE_ID,
  ACTION_CHECK_HEALTH,
  ACTION_START_FINGERPRINT_AUTH_SESSION,
  ACTION_END_FINGERPRINT_AUTH_SESSION,
};
static const char kUserSwitch[] = "user";
static const char kPasswordSwitch[] = "password";
static const char kFingerprintSwitch[] = "fingerprint";
static const char kKeyLabelSwitch[] = "key_label";
static const char kKeyRevisionSwitch[] = "key_revision";
static const char kHmacSigningKeySwitch[] = "hmac_signing_key";
static const char kNewKeyLabelSwitch[] = "new_key_label";
static const char kRemoveKeyLabelSwitch[] = "remove_key_label";
static const char kOldPasswordSwitch[] = "old_password";
static const char kNewPasswordSwitch[] = "new_password";
static const char kForceSwitch[] = "force";
static const char kAsyncSwitch[] = "async";
static const char kCreateSwitch[] = "create";
static const char kAttrNameSwitch[] = "name";
static const char kAttrPrefixSwitch[] = "prefix";
static const char kAttrValueSwitch[] = "value";
static const char kFileSwitch[] = "file";
static const char kInputFileSwitch[] = "input";
static const char kOutputFileSwitch[] = "output";
static const char kEnsureEphemeralSwitch[] = "ensure_ephemeral";
static const char kCrosCoreSwitch[] = "cros_core";
static const char kProtobufSwitch[] = "protobuf";
static const char kFlagsSwitch[] = "flags";
static const char kDevKeyHashSwitch[] = "developer_key_hash";
static const char kEcryptfsSwitch[] = "ecryptfs";
static const char kToMigrateFromEcryptfsSwitch[] = "to_migrate_from_ecryptfs";
static const char kHiddenMount[] = "hidden_mount";
static const char kMinimalMigration[] = "minimal_migration";
static const char kPublicMount[] = "public_mount";
static const char kKeyPolicySwitch[] = "key_policy";
static const char kKeyPolicyLECredential[] = "le";
static const char kProfileSwitch[] = "profile";
static const char kIgnoreCache[] = "ignore_cache";
static const char kRestoreKeyInHexSwitch[] = "restore_key_in_hex";
static const char kMassRemoveExemptLabelsSwitch[] = "exempt_key_labels";
static const char kEnrollSwitch[] = "enroll";
static const char kUseDBus[] = "use_dbus";
}  // namespace switches

#define DBUS_METHOD(method_name) org_chromium_CryptohomeInterface_##method_name

typedef void (*ProtoDBusReplyMethod)(DBusGProxy*, GArray*, GError*, gpointer);
typedef gboolean (*ProtoDBusMethod)(DBusGProxy*,
                                    const GArray*,
                                    GArray**,
                                    GError**);
typedef DBusGProxyCall* (*ProtoDBusAsyncMethod)(DBusGProxy*,
                                                const GArray*,
                                                ProtoDBusReplyMethod,
                                                gpointer);

brillo::SecureBlob GetSystemSalt(const brillo::dbus::Proxy& proxy) {
  brillo::glib::ScopedError error;
  brillo::glib::ScopedArray salt;
  if (!org_chromium_CryptohomeInterface_get_system_salt(
          proxy.gproxy(), &brillo::Resetter(&salt).lvalue(),
          &brillo::Resetter(&error).lvalue())) {
    LOG(ERROR) << "GetSystemSalt failed: " << error->message;
    return brillo::SecureBlob();
  }

  brillo::SecureBlob system_salt;
  system_salt.resize(salt->len);
  if (system_salt.size() == salt->len) {
    memcpy(system_salt.data(), static_cast<const void*>(salt->data), salt->len);
  } else {
    system_salt.clear();
  }
  return system_salt;
}

bool GetAttrName(const base::CommandLine* cl, std::string* name_out) {
  *name_out = cl->GetSwitchValueASCII(switches::kAttrNameSwitch);

  if (name_out->length() == 0) {
    printf("No install attribute name specified (--name=<name>)\n");
    return false;
  }
  return true;
}

bool GetAttrValue(const base::CommandLine* cl, std::string* value_out) {
  *value_out = cl->GetSwitchValueASCII(switches::kAttrValueSwitch);

  if (value_out->length() == 0) {
    printf("No install attribute value specified (--value=<value>)\n");
    return false;
  }
  return true;
}

bool GetAccountId(const base::CommandLine* cl, std::string* user_out) {
  *user_out = cl->GetSwitchValueASCII(switches::kUserSwitch);

  if (user_out->length() == 0) {
    printf("No user specified (--user=<account_id>)\n");
    return false;
  }
  return true;
}

bool GetPassword(const brillo::dbus::Proxy& proxy,
                 const base::CommandLine* cl,
                 const std::string& cl_switch,
                 const std::string& prompt,
                 std::string* password_out) {
  std::string password = cl->GetSwitchValueASCII(cl_switch);

  if (password.length() == 0) {
    char buffer[256];
    struct termios original_attr;
    struct termios new_attr;
    tcgetattr(0, &original_attr);
    memcpy(&new_attr, &original_attr, sizeof(new_attr));
    new_attr.c_lflag &= ~(ECHO);
    tcsetattr(0, TCSANOW, &new_attr);
    printf("%s: ", prompt.c_str());
    fflush(stdout);
    if (fgets(buffer, base::size(buffer), stdin))
      password = buffer;
    printf("\n");
    tcsetattr(0, TCSANOW, &original_attr);
  }

  std::string trimmed_password;
  base::TrimString(password, "\r\n", &trimmed_password);
  SecureBlob passkey;
  cryptohome::Crypto::PasswordToPasskey(trimmed_password.c_str(),
                                        GetSystemSalt(proxy), &passkey);
  *password_out = passkey.to_string();

  return true;
}

bool IsMixingOldAndNewFileSwitches(const base::CommandLine* cl) {
  return cl->HasSwitch(switches::kFileSwitch) &&
         (cl->HasSwitch(switches::kInputFileSwitch) ||
          cl->HasSwitch(switches::kOutputFileSwitch));
}

FilePath GetFile(const base::CommandLine* cl) {
  const char kDefaultFilePath[] = "/tmp/__cryptohome";
  FilePath file_path(cl->GetSwitchValueASCII(switches::kFileSwitch));
  if (file_path.empty()) {
    return FilePath(kDefaultFilePath);
  }
  return file_path;
}

FilePath GetInputFile(const base::CommandLine* cl) {
  FilePath file_path(cl->GetSwitchValueASCII(switches::kInputFileSwitch));
  if (file_path.empty()) {
    return GetFile(cl);
  }
  return file_path;
}

FilePath GetOutputFile(const base::CommandLine* cl) {
  FilePath file_path(cl->GetSwitchValueASCII(switches::kOutputFileSwitch));
  if (file_path.empty()) {
    return GetFile(cl);
  }
  return file_path;
}

bool GetProfile(const base::CommandLine* cl,
                cryptohome::CertificateProfile* profile) {
  const std::string profile_str =
      cl->GetSwitchValueASCII(switches::kProfileSwitch);
  if (profile_str.empty() || profile_str == "enterprise_user" ||
      profile_str == "user" || profile_str == "u") {
    *profile = cryptohome::ENTERPRISE_USER_CERTIFICATE;
  } else if (profile_str == "enterprise_machine" || profile_str == "machine" ||
             profile_str == "m") {
    *profile = cryptohome::ENTERPRISE_MACHINE_CERTIFICATE;
  } else if (profile_str == "enterprise_enrollment" ||
             profile_str == "enrollment" || profile_str == "e") {
    *profile = cryptohome::ENTERPRISE_ENROLLMENT_CERTIFICATE;
  } else if (profile_str == "content_protection" || profile_str == "content" ||
             profile_str == "c") {
    *profile = cryptohome::CONTENT_PROTECTION_CERTIFICATE;
  } else if (profile_str == "content_protection_with_stable_id" ||
             profile_str == "cpsi") {
    *profile = cryptohome::CONTENT_PROTECTION_CERTIFICATE_WITH_STABLE_ID;
  } else if (profile_str == "cast") {
    *profile = cryptohome::CAST_CERTIFICATE;
  } else if (profile_str == "gfsc") {
    *profile = cryptohome::GFSC_CERTIFICATE;
  } else if (profile_str == "jetstream") {
    *profile = cryptohome::JETSTREAM_CERTIFICATE;
  } else {
    printf("Unknown certificate profile: %s.\n", profile_str.c_str());
    return false;
  }
  return true;
}

bool ConfirmRemove(const std::string& user) {
  printf("!!! Are you sure you want to remove the user's cryptohome?\n");
  printf("!!!\n");
  printf("!!! Re-enter the username at the prompt to remove the\n");
  printf("!!! cryptohome for the user.\n");
  printf("Enter the username <%s>: ", user.c_str());
  fflush(stdout);

  char buffer[256];
  if (!fgets(buffer, base::size(buffer), stdin)) {
    printf("Error while reading username.\n");
    return false;
  }
  std::string verification = buffer;
  // fgets will append the newline character, remove it.
  base::TrimWhitespaceASCII(verification, base::TRIM_ALL, &verification);
  if (user != verification) {
    printf("Usernames do not match.\n");
    return false;
  }
  return true;
}

GArray* GArrayFromProtoBuf(const google::protobuf::MessageLite& pb) {
  size_t len_raw = pb.ByteSizeLong();
  if (len_raw > G_MAXUINT) {
    printf("Protocol buffer too large.\n");
    return NULL;
  }

  guint len = len_raw;
  GArray* ary = g_array_sized_new(FALSE, FALSE, 1, len);
  g_array_set_size(ary, len);
  if (!pb.SerializeToArray(ary->data, len)) {
    printf("Failed to serialize protocol buffer.\n");
    return NULL;
  }
  return ary;
}

bool BuildAccountId(base::CommandLine* cl, cryptohome::AccountIdentifier* id) {
  std::string account_id;
  if (!GetAccountId(cl, &account_id)) {
    printf("No account_id specified.\n");
    return false;
  }
  id->set_account_id(account_id);
  return true;
}

bool BuildAuthorization(base::CommandLine* cl,
                        const brillo::dbus::Proxy& proxy,
                        bool need_password,
                        cryptohome::AuthorizationRequest* auth) {
  if (need_password) {
    // Check if restore key is provided
    if (cl->HasSwitch(switches::kRestoreKeyInHexSwitch)) {
      brillo::SecureBlob raw_byte(
          cl->GetSwitchValueASCII(switches::kRestoreKeyInHexSwitch));
      if (raw_byte.to_string().length() == 0) {
        printf("No hex string specified\n");
        return false;
      }
      SecureBlob::HexStringToSecureBlob(raw_byte.to_string(), &raw_byte);
      auth->mutable_key()->set_secret(raw_byte.to_string());
    } else {
      std::string password;
      GetPassword(proxy, cl, switches::kPasswordSwitch, "Enter the password",
                  &password);

      auth->mutable_key()->set_secret(password);
    }
  }

  if (cl->HasSwitch(switches::kKeyLabelSwitch)) {
    auth->mutable_key()->mutable_data()->set_label(
        cl->GetSwitchValueASCII(switches::kKeyLabelSwitch));
  }

  return true;
}

void ParseBaseReply(GArray* reply_ary,
                    cryptohome::BaseReply* reply,
                    bool print_reply) {
  if (!reply)
    return;
  if (!reply->ParseFromArray(reply_ary->data, reply_ary->len)) {
    printf("Failed to parse reply.\n");
    exit(1);
  }
  if (print_reply)
    reply->PrintDebugString();
}

class ClientLoop {
 public:
  ClientLoop()
      : loop_(NULL),
        async_call_id_(0),
        return_status_(false),
        return_code_(0) {}

  virtual ~ClientLoop() {
    if (loop_) {
      g_main_loop_unref(loop_);
    }
  }

  void Initialize(brillo::dbus::Proxy* proxy) {
    dbus_g_object_register_marshaller(g_cclosure_marshal_generic, G_TYPE_NONE,
                                      G_TYPE_INT, G_TYPE_BOOLEAN, G_TYPE_INT,
                                      G_TYPE_INVALID);
    dbus_g_proxy_add_signal(proxy->gproxy(), "AsyncCallStatus", G_TYPE_INT,
                            G_TYPE_BOOLEAN, G_TYPE_INT, G_TYPE_INVALID);
    dbus_g_proxy_connect_signal(proxy->gproxy(), "AsyncCallStatus",
                                G_CALLBACK(ClientLoop::CallbackThunk), this,
                                NULL);
    dbus_g_object_register_marshaller(g_cclosure_marshal_generic, G_TYPE_NONE,
                                      G_TYPE_INT, G_TYPE_BOOLEAN,
                                      DBUS_TYPE_G_UCHAR_ARRAY, G_TYPE_INVALID);
    dbus_g_proxy_add_signal(proxy->gproxy(), "AsyncCallStatusWithData",
                            G_TYPE_INT, G_TYPE_BOOLEAN, DBUS_TYPE_G_UCHAR_ARRAY,
                            G_TYPE_INVALID);
    dbus_g_proxy_connect_signal(proxy->gproxy(), "AsyncCallStatusWithData",
                                G_CALLBACK(ClientLoop::CallbackDataThunk), this,
                                NULL);
    loop_ = g_main_loop_new(NULL, TRUE);
  }

  void Run(int async_call_id) {
    async_call_id_ = async_call_id;
    g_main_loop_run(loop_);
  }

  void Run() { Run(0); }

  // This callback can be used with a ClientLoop instance as the |userdata| to
  // handle an asynchronous reply which emits a serialized BaseReply.
  static void ParseReplyThunk(DBusGProxy* proxy,
                              GArray* data,
                              GError* error,
                              gpointer userdata) {
    reinterpret_cast<ClientLoop*>(userdata)->ParseReply(data, error);
  }

  bool get_return_status() { return return_status_; }

  int get_return_code() { return return_code_; }

  std::string get_return_data() { return return_data_; }

  cryptohome::BaseReply reply() { return reply_; }

 private:
  void Callback(int async_call_id, bool return_status, int return_code) {
    if (async_call_id == async_call_id_) {
      return_status_ = return_status;
      return_code_ = return_code;
      g_main_loop_quit(loop_);
    }
  }

  void CallbackWithData(int async_call_id, bool return_status, GArray* data) {
    if (async_call_id == async_call_id_) {
      return_status_ = return_status;
      return_data_ = std::string(static_cast<char*>(data->data), data->len);
      g_main_loop_quit(loop_);
    }
  }

  void ParseReply(GArray* reply_ary, GError* error) {
    if (error && error->message) {
      printf("Call error: %s\n", error->message);
      exit(1);
    }
    ParseBaseReply(reply_ary, &reply_, true /* print_reply */);
    g_main_loop_quit(loop_);
  }

  static void CallbackThunk(DBusGProxy* proxy,
                            int async_call_id,
                            bool return_status,
                            int return_code,
                            gpointer userdata) {
    reinterpret_cast<ClientLoop*>(userdata)->Callback(
        async_call_id, return_status, return_code);
  }

  static void CallbackDataThunk(DBusGProxy* proxy,
                                int async_call_id,
                                bool return_status,
                                GArray* data,
                                gpointer userdata) {
    reinterpret_cast<ClientLoop*>(userdata)->CallbackWithData(
        async_call_id, return_status, data);
  }

  GMainLoop* loop_;
  int async_call_id_;
  bool return_status_;
  int return_code_;
  std::string return_data_;
  cryptohome::BaseReply reply_;
};

bool MakeProtoDBusCall(const std::string& name,
                       ProtoDBusMethod method,
                       ProtoDBusAsyncMethod async_method,
                       base::CommandLine* cl,
                       brillo::dbus::Proxy* proxy,
                       const google::protobuf::MessageLite& request,
                       cryptohome::BaseReply* reply,
                       bool print_reply) {
  brillo::glib::ScopedArray request_ary(GArrayFromProtoBuf(request));
  if (cl->HasSwitch(switches::kAsyncSwitch)) {
    ClientLoop loop;
    loop.Initialize(proxy);
    DBusGProxyCall* call = (*async_method)(proxy->gproxy(), request_ary.get(),
                                           &ClientLoop::ParseReplyThunk,
                                           static_cast<gpointer>(&loop));
    if (!call) {
      printf("Failed to call %s!\n", name.c_str());
      return false;
    }
    loop.Run();
    *reply = loop.reply();
  } else {
    brillo::glib::ScopedError error;
    brillo::glib::ScopedArray reply_ary;
    if (!(*method)(proxy->gproxy(), request_ary.get(),
                   &brillo::Resetter(&reply_ary).lvalue(),
                   &brillo::Resetter(&error).lvalue())) {
      printf("Failed to call %s: %s\n", name.c_str(), error->message);
      return false;
    }
    ParseBaseReply(reply_ary.get(), reply, print_reply);
  }
  if (reply->has_error()) {
    printf("%s error: %d\n", name.c_str(), reply->error());
    return false;
  }

  return true;
}

std::string GetPCAName(int pca_type) {
  switch (pca_type) {
    case attestation::DEFAULT_ACA:
      return "the default ACA";
    case attestation::TEST_ACA:
      return "the test ACA";
    default: {
      std::ostringstream stream;
      stream << "ACA " << pca_type;
      return stream.str();
    }
  }
}

int main(int argc, char** argv) {
  base::CommandLine::Init(argc, argv);
  base::CommandLine* cl = base::CommandLine::ForCurrentProcess();
  if (cl->HasSwitch(switches::kSyslogSwitch))
    brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderr);
  else
    brillo::InitLog(brillo::kLogToStderr);

  int pca_type = attestation::ACAType_ARRAYSIZE;
  if (cl->HasSwitch(switches::kAttestationServerSwitch)) {
    std::string server =
        cl->GetSwitchValueASCII(switches::kAttestationServerSwitch);
    for (int i = 0; switches::kAttestationServers[i].name; ++i) {
      if (server == switches::kAttestationServers[i].name) {
        pca_type = switches::kAttestationServers[i].aca_type;
        break;
      }
    }
    if (pca_type == attestation::ACAType_ARRAYSIZE) {
      printf("Invalid attestation server: %s\n", server.c_str());
      return 1;
    }
  } else {
    pca_type = attestation::DEFAULT_ACA;
  }

  int va_type = attestation::VAType_ARRAYSIZE;
  std::string va_server(
      cl->HasSwitch(switches::kVaServerSwitch)
          ? cl->GetSwitchValueASCII(switches::kVaServerSwitch)
          : cl->GetSwitchValueASCII(switches::kAttestationServerSwitch));
  if (va_server.size()) {
    for (int i = 0; switches::kVaServers[i].name; ++i) {
      if (va_server == switches::kVaServers[i].name) {
        va_type = switches::kVaServers[i].va_type;
        break;
      }
    }
    if (va_type == attestation::VAType_ARRAYSIZE) {
      printf("Invalid Verified Access server: %s\n", va_server.c_str());
      return 1;
    }
  } else {
    va_type = attestation::DEFAULT_VA;
  }

  if (IsMixingOldAndNewFileSwitches(cl)) {
    printf("Use either --%s and --%s together, or --%s only.\n",
           switches::kInputFileSwitch, switches::kOutputFileSwitch,
           switches::kFileSwitch);
    return 1;
  }

  std::string action = cl->GetSwitchValueASCII(switches::kActionSwitch);
  brillo::dbus::BusConnection bus = brillo::dbus::GetSystemBusConnection();
  brillo::dbus::Proxy proxy(bus, cryptohome::kCryptohomeServiceName,
                            cryptohome::kCryptohomeServicePath,
                            cryptohome::kCryptohomeInterface);
  DCHECK(proxy.gproxy()) << "Failed to acquire proxy";
  dbus_g_proxy_set_default_timeout(proxy.gproxy(), kDefaultTimeoutMs);

  cryptohome::Platform platform;

  if (!strcmp(switches::kActions[switches::ACTION_MOUNT_EX], action.c_str())) {
    bool is_public_mount = cl->HasSwitch(switches::kPublicMount);

    cryptohome::AccountIdentifier id;
    if (!BuildAccountId(cl, &id))
      return 1;
    cryptohome::AuthorizationRequest auth;
    if (!BuildAuthorization(cl, proxy, !is_public_mount, &auth))
      return 1;

    cryptohome::MountRequest mount_req;
    mount_req.set_require_ephemeral(
        cl->HasSwitch(switches::kEnsureEphemeralSwitch));
    mount_req.set_to_migrate_from_ecryptfs(
        cl->HasSwitch(switches::kToMigrateFromEcryptfsSwitch));
    mount_req.set_hidden_mount(cl->HasSwitch(switches::kHiddenMount));
    mount_req.set_public_mount(is_public_mount);
    if (cl->HasSwitch(switches::kCreateSwitch)) {
      cryptohome::CreateRequest* create = mount_req.mutable_create();
      if (cl->HasSwitch(switches::kPublicMount)) {
        cryptohome::Key* key = create->add_keys();
        key->mutable_data()->set_label(auth.key().data().label());
        // The proto definition defaults all privileges to true but we only want
        // mount, add, remove, and update.
        key->mutable_data()->mutable_privileges()->set_authorized_update(false);
      } else {
        create->set_copy_authorization_key(true);
      }
      if (cl->HasSwitch(switches::kEcryptfsSwitch)) {
        create->set_force_ecryptfs(true);
      }
    }

    brillo::glib::ScopedArray account_ary(GArrayFromProtoBuf(id));
    brillo::glib::ScopedArray auth_ary(GArrayFromProtoBuf(auth));
    brillo::glib::ScopedArray req_ary(GArrayFromProtoBuf(mount_req));
    if (!account_ary.get() || !auth_ary.get() || !req_ary.get())
      return 1;

    cryptohome::BaseReply reply;
    brillo::glib::ScopedError error;
    if (cl->HasSwitch(switches::kAsyncSwitch)) {
      ClientLoop loop;
      loop.Initialize(&proxy);
      DBusGProxyCall* call = org_chromium_CryptohomeInterface_mount_ex_async(
          proxy.gproxy(), account_ary.get(), auth_ary.get(), req_ary.get(),
          &ClientLoop::ParseReplyThunk, static_cast<gpointer>(&loop));
      if (!call)
        return 1;
      loop.Run();
      reply = loop.reply();
    } else {
      GArray* out_reply = NULL;
      if (!org_chromium_CryptohomeInterface_mount_ex(
              proxy.gproxy(), account_ary.get(), auth_ary.get(), req_ary.get(),
              &out_reply, &brillo::Resetter(&error).lvalue())) {
        printf("MountEx call failed: %s", error->message);
        return 1;
      }
      ParseBaseReply(out_reply, &reply, true /* print_reply */);
    }
    if (reply.has_error()) {
      printf("Mount failed.\n");
      return reply.error();
    }
    printf("Mount succeeded.\n");
  } else if (!strcmp(switches::kActions[switches::ACTION_MOUNT_GUEST_EX],
                     action.c_str())) {
    cryptohome::BaseReply reply;
    cryptohome::MountGuestRequest request;
    brillo::glib::ScopedError error;
    GArray* out_reply = NULL;

    brillo::glib::ScopedArray guest_request_ary(GArrayFromProtoBuf(request));
    if (!org_chromium_CryptohomeInterface_mount_guest_ex(
            proxy.gproxy(), guest_request_ary.get(), &out_reply,
            &brillo::Resetter(&error).lvalue())) {
      printf("Mount call failed: %s.\n", error->message);
      return 1;
    }

    ParseBaseReply(out_reply, &reply, true /* print_reply */);
    if (reply.has_error()) {
      printf("Mount failed.\n");
      return reply.error();
    }
    printf("Mount succeeded.\n");
  } else if (!strcmp(switches::kActions
                         [switches::ACTION_START_FINGERPRINT_AUTH_SESSION],
                     action.c_str())) {
    cryptohome::AccountIdentifier id;
    if (!BuildAccountId(cl, &id))
      return 1;

    cryptohome::StartFingerprintAuthSessionRequest req;

    brillo::glib::ScopedArray account_ary(GArrayFromProtoBuf(id));
    brillo::glib::ScopedArray req_ary(GArrayFromProtoBuf(req));
    if (!account_ary.get() || !req_ary.get())
      return 1;

    cryptohome::BaseReply reply;
    brillo::glib::ScopedError error;

    GArray* out_reply = NULL;
    if (!org_chromium_CryptohomeInterface_start_fingerprint_auth_session(
            proxy.gproxy(), account_ary.get(), req_ary.get(), &out_reply,
            &brillo::Resetter(&error).lvalue())) {
      printf("StartFingerprintAuthSession call failed: %s", error->message);
      return 1;
    }
    ParseBaseReply(out_reply, &reply, true /* print_reply */);
    if (reply.has_error()) {
      printf("Fingerprint auth session failed to start.\n");
      return reply.error();
    }
  } else if (!strcmp(switches::kActions
                         [switches::ACTION_END_FINGERPRINT_AUTH_SESSION],
                     action.c_str())) {
    cryptohome::EndFingerprintAuthSessionRequest req;
    brillo::glib::ScopedArray req_ary(GArrayFromProtoBuf(req));
    if (!req_ary.get())
      return 1;

    brillo::glib::ScopedError error;
    GArray* out_reply = NULL;
    if (!org_chromium_CryptohomeInterface_end_fingerprint_auth_session(
            proxy.gproxy(), req_ary.get(), &out_reply,
            &brillo::Resetter(&error).lvalue())) {
      printf("EndFingerprintAuthSession call failed: %s", error->message);
      return 1;
    }
    // EndFingerprintAuthSession always succeeds.
  } else if (!strcmp(switches::kActions[switches::ACTION_REMOVE_KEY_EX],
                     action.c_str())) {
    cryptohome::AccountIdentifier id;
    if (!BuildAccountId(cl, &id))
      return 1;
    cryptohome::AuthorizationRequest auth;
    if (!BuildAuthorization(cl, proxy, true /* need_password */, &auth))
      return 1;

    cryptohome::RemoveKeyRequest remove_req;
    cryptohome::KeyData* data = remove_req.mutable_key()->mutable_data();
    data->set_label(cl->GetSwitchValueASCII(switches::kRemoveKeyLabelSwitch));

    brillo::glib::ScopedArray account_ary(GArrayFromProtoBuf(id));
    brillo::glib::ScopedArray auth_ary(GArrayFromProtoBuf(auth));
    brillo::glib::ScopedArray req_ary(GArrayFromProtoBuf(remove_req));
    if (!account_ary.get() || !auth_ary.get() || !req_ary.get())
      return 1;

    cryptohome::BaseReply reply;
    brillo::glib::ScopedError error;
    if (cl->HasSwitch(switches::kAsyncSwitch)) {
      ClientLoop loop;
      loop.Initialize(&proxy);
      DBusGProxyCall* call =
          org_chromium_CryptohomeInterface_remove_key_ex_async(
              proxy.gproxy(), account_ary.get(), auth_ary.get(), req_ary.get(),
              &ClientLoop::ParseReplyThunk, static_cast<gpointer>(&loop));
      if (!call)
        return 1;
      loop.Run();
      reply = loop.reply();
    } else {
      GArray* out_reply = NULL;
      if (!org_chromium_CryptohomeInterface_remove_key_ex(
              proxy.gproxy(), account_ary.get(), auth_ary.get(), req_ary.get(),
              &out_reply, &brillo::Resetter(&error).lvalue())) {
        printf("RemoveKeyEx call failed: %s", error->message);
        return 1;
      }
      ParseBaseReply(out_reply, &reply, true /* print_reply */);
    }
    if (reply.has_error()) {
      printf("Key removal failed.\n");
      return reply.error();
    }
    printf("Key removed.\n");
  } else if (!strcmp(switches::kActions[switches::ACTION_GET_KEY_DATA_EX],
                     action.c_str())) {
    cryptohome::AccountIdentifier id;
    if (!BuildAccountId(cl, &id)) {
      return 1;
    }
    cryptohome::AuthorizationRequest auth;
    cryptohome::GetKeyDataRequest key_data_req;
    const std::string label =
        cl->GetSwitchValueASCII(switches::kKeyLabelSwitch);
    if (label.empty()) {
      printf("No key_label specified.\n");
      return 1;
    }
    key_data_req.mutable_key()->mutable_data()->set_label(label);

    brillo::glib::ScopedArray account_ary(GArrayFromProtoBuf(id));
    brillo::glib::ScopedArray auth_ary(GArrayFromProtoBuf(auth));
    brillo::glib::ScopedArray req_ary(GArrayFromProtoBuf(key_data_req));
    if (!account_ary.get() || !auth_ary.get() || !req_ary.get()) {
      return 1;
    }

    cryptohome::BaseReply reply;
    brillo::glib::ScopedError error;
    if (cl->HasSwitch(switches::kAsyncSwitch)) {
      ClientLoop loop;
      loop.Initialize(&proxy);
      DBusGProxyCall* call =
          org_chromium_CryptohomeInterface_get_key_data_ex_async(
              proxy.gproxy(), account_ary.get(), auth_ary.get(), req_ary.get(),
              &ClientLoop::ParseReplyThunk, static_cast<gpointer>(&loop));
      if (!call) {
        return 1;
      }
      loop.Run();
      reply = loop.reply();
    } else {
      GArray* out_reply = NULL;
      if (!org_chromium_CryptohomeInterface_get_key_data_ex(
              proxy.gproxy(), account_ary.get(), auth_ary.get(), req_ary.get(),
              &out_reply, &brillo::Resetter(&error).lvalue())) {
        printf("GetKeyDataEx call failed: %s", error->message);
        return 1;
      }
      ParseBaseReply(out_reply, &reply, true /* print_reply */);
    }
    if (reply.has_error()) {
      printf("Key retrieval failed.\n");
      return reply.error();
    }
  } else if (!strcmp(switches::kActions[switches::ACTION_LIST_KEYS_EX],
                     action.c_str())) {
    cryptohome::AccountIdentifier id;
    if (!BuildAccountId(cl, &id))
      return 1;
    cryptohome::AuthorizationRequest auth;

    cryptohome::ListKeysRequest list_keys_req;

    brillo::glib::ScopedArray account_ary(GArrayFromProtoBuf(id));
    brillo::glib::ScopedArray auth_ary(GArrayFromProtoBuf(auth));
    brillo::glib::ScopedArray req_ary(GArrayFromProtoBuf(list_keys_req));
    if (!account_ary.get() || !auth_ary.get() || !req_ary.get())
      return 1;

    cryptohome::BaseReply reply;
    brillo::glib::ScopedError error;
    if (cl->HasSwitch(switches::kAsyncSwitch)) {
      ClientLoop loop;
      loop.Initialize(&proxy);
      DBusGProxyCall* call =
          org_chromium_CryptohomeInterface_list_keys_ex_async(
              proxy.gproxy(), account_ary.get(), auth_ary.get(), req_ary.get(),
              &ClientLoop::ParseReplyThunk, static_cast<gpointer>(&loop));
      if (!call) {
        return 1;
      }
      loop.Run();
      reply = loop.reply();
    } else {
      GArray* out_reply = NULL;
      if (!org_chromium_CryptohomeInterface_list_keys_ex(
              proxy.gproxy(), account_ary.get(), auth_ary.get(), req_ary.get(),
              &out_reply, &brillo::Resetter(&error).lvalue())) {
        printf("ListKeysEx call failed: %s", error->message);
        return 1;
      }
      ParseBaseReply(out_reply, &reply, true /* print_reply */);
    }
    if (reply.has_error()) {
      printf("Failed to list keys.\n");
      return reply.error();
    }
    if (!reply.HasExtension(cryptohome::ListKeysReply::reply)) {
      printf("ListKeysReply missing.\n");
      return 1;
    }
    cryptohome::ListKeysReply list_keys_reply =
        reply.GetExtension(cryptohome::ListKeysReply::reply);
    for (int i = 0; i < list_keys_reply.labels_size(); ++i) {
      printf("Label: %s\n", list_keys_reply.labels(i).c_str());
    }
  } else if (!strcmp(switches::kActions[switches::ACTION_CHECK_KEY_EX],
                     action.c_str())) {
    cryptohome::AccountIdentifier id;
    if (!BuildAccountId(cl, &id))
      return 1;
    cryptohome::AuthorizationRequest auth;
    if (cl->HasSwitch(switches::kFingerprintSwitch)) {
      auth.mutable_key()->mutable_data()->set_type(
          cryptohome::KeyData::KEY_TYPE_FINGERPRINT);
    } else if (!BuildAuthorization(cl, proxy, true /* need_password */,
                                   &auth)) {
      return 1;
    }

    cryptohome::CheckKeyRequest check_req;
    // TODO(wad) Add a privileges cl interface

    brillo::glib::ScopedArray account_ary(GArrayFromProtoBuf(id));
    brillo::glib::ScopedArray auth_ary(GArrayFromProtoBuf(auth));
    brillo::glib::ScopedArray req_ary(GArrayFromProtoBuf(check_req));
    if (!account_ary.get() || !auth_ary.get() || !req_ary.get())
      return 1;

    cryptohome::BaseReply reply;
    brillo::glib::ScopedError error;
    if (cl->HasSwitch(switches::kAsyncSwitch)) {
      ClientLoop loop;
      loop.Initialize(&proxy);
      DBusGProxyCall* call =
          org_chromium_CryptohomeInterface_check_key_ex_async(
              proxy.gproxy(), account_ary.get(), auth_ary.get(), req_ary.get(),
              &ClientLoop::ParseReplyThunk, static_cast<gpointer>(&loop));
      if (!call)
        return 1;
      loop.Run();
      reply = loop.reply();
    } else {
      GArray* out_reply = NULL;
      if (!org_chromium_CryptohomeInterface_check_key_ex(
              proxy.gproxy(), account_ary.get(), auth_ary.get(), req_ary.get(),
              &out_reply, &brillo::Resetter(&error).lvalue())) {
        printf("CheckKeyEx call failed: %s", error->message);
        return 1;
      }
      ParseBaseReply(out_reply, &reply, true /* print_reply */);
    }
    if (reply.has_error()) {
      printf("Key authentication failed.\n");
      return reply.error();
    }
    printf("Key authenticated.\n");
  } else if (!strcmp(switches::kActions[switches::ACTION_ADD_DATA_RESTORE_KEY],
                     action.c_str())) {
    cryptohome::AccountIdentifier id;
    if (!BuildAccountId(cl, &id))
      return 1;
    cryptohome::AuthorizationRequest auth;
    if (!BuildAuthorization(cl, proxy, true /* need_password */, &auth))
      return 1;

    brillo::glib::ScopedArray account_ary(GArrayFromProtoBuf(id));
    brillo::glib::ScopedArray auth_ary(GArrayFromProtoBuf(auth));
    if (!account_ary.get() || !auth_ary.get())
      return 1;

    cryptohome::BaseReply reply;
    brillo::glib::ScopedError error;
    if (cl->HasSwitch(switches::kAsyncSwitch)) {
      ClientLoop loop;
      loop.Initialize(&proxy);
      DBusGProxyCall* call =
          org_chromium_CryptohomeInterface_add_data_restore_key_async(
              proxy.gproxy(), account_ary.get(), auth_ary.get(),
              &ClientLoop::ParseReplyThunk, static_cast<gpointer>(&loop));
      if (!call)
        return 1;
      loop.Run();
      reply = loop.reply();
    } else {
      GArray* out_reply = NULL;
      if (!org_chromium_CryptohomeInterface_add_data_restore_key(
              proxy.gproxy(), account_ary.get(), auth_ary.get(), &out_reply,
              &brillo::Resetter(&error).lvalue())) {
        printf("Restore key addition failed: %s", error->message);
        return 1;
      }
      ParseBaseReply(out_reply, &reply, true /* print_reply */);
    }
    if (reply.has_error()) {
      printf("Restore key addition failed.\n");
      return reply.error();
    }
    SecureBlob data_restore_key_raw(
        reply.GetExtension(cryptohome::AddDataRestoreKeyReply::reply)
            .data_restore_key());
    printf("Restore key addition succeeded.\n");
    printf("Here's the data restore key in hex: %s\n",
           brillo::SecureBlobToSecureHex(data_restore_key_raw)
               .to_string()
               .c_str());
  } else if (!strcmp(switches::kActions[switches::ACTION_MASS_REMOVE_KEYS],
                     action.c_str())) {
    cryptohome::AccountIdentifier id;
    if (!BuildAccountId(cl, &id))
      return 1;
    cryptohome::AuthorizationRequest auth;
    if (!BuildAuthorization(cl, proxy, true /* need_password */, &auth))
      return 1;

    cryptohome::MassRemoveKeysRequest mass_remove_keys_request;
    // Since it's unlikely to have comma in a label string,
    // exempt_key_labels are seperated by comma from command line input
    // ( e.g. --exempt_key_labels=label1,label2,label3 )
    std::vector<std::string> exempt_labels = SplitString(
        cl->GetSwitchValueASCII(switches::kMassRemoveExemptLabelsSwitch), ",",
        base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
    for (std::string label : exempt_labels) {
      cryptohome::KeyData* data =
          mass_remove_keys_request.add_exempt_key_data();
      data->set_label(label);
    }
    brillo::glib::ScopedArray account_ary(GArrayFromProtoBuf(id));
    brillo::glib::ScopedArray auth_ary(GArrayFromProtoBuf(auth));
    brillo::glib::ScopedArray req_ary(
        GArrayFromProtoBuf(mass_remove_keys_request));
    if (!account_ary.get() || !auth_ary.get() || !req_ary.get())
      return 1;

    cryptohome::BaseReply reply;
    brillo::glib::ScopedError error;
    if (cl->HasSwitch(switches::kAsyncSwitch)) {
      ClientLoop loop;
      loop.Initialize(&proxy);
      DBusGProxyCall* call =
          org_chromium_CryptohomeInterface_mass_remove_keys_async(
              proxy.gproxy(), account_ary.get(), auth_ary.get(), req_ary.get(),
              &ClientLoop::ParseReplyThunk, static_cast<gpointer>(&loop));
      if (!call)
        return 1;
      loop.Run();
      reply = loop.reply();
    } else {
      GArray* out_reply = NULL;
      if (!org_chromium_CryptohomeInterface_mass_remove_keys(
              proxy.gproxy(), account_ary.get(), auth_ary.get(), req_ary.get(),
              &out_reply, &brillo::Resetter(&error).lvalue())) {
        printf("MassRemoveKeys call failed: %s", error->message);
        return 1;
      }
      ParseBaseReply(out_reply, &reply, true /* print_reply */);
    }
    if (reply.has_error()) {
      printf("MassRemoveKeys failed.\n");
      return reply.error();
    }
    printf("MassRemoveKeys succeeded.\n");
  } else if (!strcmp(switches::kActions[switches::ACTION_MIGRATE_KEY_EX],
                     action.c_str())) {
    std::string account_id, password, old_password;

    if (!GetAccountId(cl, &account_id)) {
      return 1;
    }

    GetPassword(proxy, cl, switches::kPasswordSwitch,
                StringPrintf("Enter the password for <%s>", account_id.c_str()),
                &password);
    GetPassword(
        proxy, cl, switches::kOldPasswordSwitch,
        StringPrintf("Enter the old password for <%s>", account_id.c_str()),
        &old_password);

    cryptohome::AccountIdentifier account;
    cryptohome::AuthorizationRequest auth_request;
    cryptohome::MigrateKeyRequest migrate_request;
    account.set_account_id(account_id);
    auth_request.mutable_key()->set_secret(old_password);
    migrate_request.set_secret(password);

    brillo::glib::ScopedArray account_ary(GArrayFromProtoBuf(account));
    brillo::glib::ScopedArray auth_request_ary(
        GArrayFromProtoBuf(auth_request));
    brillo::glib::ScopedArray migrate_request_ary(
        GArrayFromProtoBuf(migrate_request));

    if (!account_ary.get() || !auth_request_ary.get() ||
        !migrate_request_ary.get()) {
      return 1;
    }

    cryptohome::BaseReply reply;
    brillo::glib::ScopedError error;
    GArray* out_reply = NULL;
    if (!org_chromium_CryptohomeInterface_migrate_key_ex(
            proxy.gproxy(), account_ary.get(), auth_request_ary.get(),
            migrate_request_ary.get(), &out_reply,
            &brillo::Resetter(&error).lvalue())) {
      printf("MigrateKeyEx call failed: %s", error->message);
      return 1;
    }
    ParseBaseReply(out_reply, &reply, true /* print_reply */);
    if (reply.has_error()) {
      printf("Key migration failed.\n");
      return reply.error();
    }
    printf("Key migration succeeded.\n");
  } else if (!strcmp(switches::kActions[switches::ACTION_ADD_KEY_EX],
                     action.c_str())) {
    std::string new_password;
    GetPassword(proxy, cl, switches::kNewPasswordSwitch,
                "Enter the new password", &new_password);
    cryptohome::AccountIdentifier id;
    if (!BuildAccountId(cl, &id))
      return 1;
    cryptohome::AuthorizationRequest auth;
    if (!BuildAuthorization(cl, proxy, true /* need_password */, &auth))
      return 1;

    cryptohome::AddKeyRequest key_req;
    key_req.set_clobber_if_exists(cl->HasSwitch(switches::kForceSwitch));

    cryptohome::Key* key = key_req.mutable_key();
    key->set_secret(new_password);
    cryptohome::KeyData* data = key->mutable_data();
    data->set_label(cl->GetSwitchValueASCII(switches::kNewKeyLabelSwitch));

    if (cl->HasSwitch(switches::kHmacSigningKeySwitch)) {
      cryptohome::KeyAuthorizationData* auth_data =
          data->add_authorization_data();
      auth_data->set_type(
          cryptohome::KeyAuthorizationData::KEY_AUTHORIZATION_TYPE_HMACSHA256);
      cryptohome::KeyAuthorizationSecret* auth_secret =
          auth_data->add_secrets();
      auth_secret->mutable_usage()->set_sign(true);
      auth_secret->set_symmetric_key(
          cl->GetSwitchValueASCII(switches::kHmacSigningKeySwitch));

      LOG(INFO) << "Adding restricted key";
      cryptohome::KeyPrivileges* privs = data->mutable_privileges();
      privs->set_mount(true);
      privs->set_authorized_update(true);
      privs->set_update(false);
      privs->set_add(false);
      privs->set_remove(false);
    }

    if (cl->HasSwitch(switches::kKeyPolicySwitch)) {
      if (cl->GetSwitchValueASCII(switches::kKeyPolicySwitch) ==
          switches::kKeyPolicyLECredential) {
        data->mutable_policy()->set_low_entropy_credential(true);
      } else {
        printf("Unknown key policy.\n");
        return 1;
      }
    }

    // TODO(wad) Add a privileges cl interface

    brillo::glib::ScopedArray account_ary(GArrayFromProtoBuf(id));
    brillo::glib::ScopedArray auth_ary(GArrayFromProtoBuf(auth));
    brillo::glib::ScopedArray req_ary(GArrayFromProtoBuf(key_req));
    if (!account_ary.get() || !auth_ary.get() || !req_ary.get())
      return 1;

    cryptohome::BaseReply reply;
    brillo::glib::ScopedError error;
    if (cl->HasSwitch(switches::kAsyncSwitch)) {
      ClientLoop loop;
      loop.Initialize(&proxy);
      DBusGProxyCall* call = org_chromium_CryptohomeInterface_add_key_ex_async(
          proxy.gproxy(), account_ary.get(), auth_ary.get(), req_ary.get(),
          &ClientLoop::ParseReplyThunk, static_cast<gpointer>(&loop));
      if (!call)
        return 1;
      loop.Run();
      reply = loop.reply();
    } else {
      GArray* out_reply = NULL;
      if (!org_chromium_CryptohomeInterface_add_key_ex(
              proxy.gproxy(), account_ary.get(), auth_ary.get(), req_ary.get(),
              &out_reply, &brillo::Resetter(&error).lvalue())) {
        printf("AddKeyEx call failed: %s", error->message);
        return 1;
      }
      ParseBaseReply(out_reply, &reply, true /* print_reply */);
    }
    if (reply.has_error()) {
      printf("Key addition failed.\n");
      return reply.error();
    }
    printf("Key added.\n");
  } else if (!strcmp(switches::kActions[switches::ACTION_UPDATE_KEY_EX],
                     action.c_str())) {
    std::string new_password;
    GetPassword(proxy, cl, switches::kNewPasswordSwitch,
                "Enter the new password", &new_password);
    cryptohome::AccountIdentifier id;
    if (!BuildAccountId(cl, &id))
      return 1;
    cryptohome::AuthorizationRequest auth;
    if (!BuildAuthorization(cl, proxy, true /* need_password */, &auth))
      return 1;

    cryptohome::UpdateKeyRequest key_req;
    cryptohome::Key* key = key_req.mutable_changes();
    key->set_secret(new_password);
    cryptohome::KeyData* data = key->mutable_data();
    if (cl->HasSwitch(switches::kNewKeyLabelSwitch))
      data->set_label(cl->GetSwitchValueASCII(switches::kNewKeyLabelSwitch));

    if (cl->HasSwitch(switches::kKeyRevisionSwitch)) {
      int int_value = 0;
      if (!base::StringToInt(
              cl->GetSwitchValueASCII(switches::kKeyRevisionSwitch),
              &int_value))
        LOG(FATAL) << "Cannot parse --key_revision";
      data->set_revision(int_value);
    }

    if (cl->HasSwitch(switches::kHmacSigningKeySwitch)) {
      ac::chrome::managedaccounts::account::Secret new_secret;
      new_secret.set_revision(data->revision());
      new_secret.set_secret(key->secret());
      std::string changes_str;
      if (!new_secret.SerializeToString(&changes_str)) {
        LOG(FATAL) << "Failed to serialize Secret";
      }
      brillo::SecureBlob hmac_key(
          cl->GetSwitchValueASCII(switches::kHmacSigningKeySwitch));
      brillo::SecureBlob hmac_data(changes_str.begin(), changes_str.end());
      SecureBlob hmac = cryptohome::CryptoLib::HmacSha256(hmac_key, hmac_data);
      key_req.set_authorization_signature(hmac.to_string());
    }

    brillo::glib::ScopedArray account_ary(GArrayFromProtoBuf(id));
    brillo::glib::ScopedArray auth_ary(GArrayFromProtoBuf(auth));
    brillo::glib::ScopedArray req_ary(GArrayFromProtoBuf(key_req));
    if (!account_ary.get() || !auth_ary.get() || !req_ary.get())
      return 1;

    cryptohome::BaseReply reply;
    brillo::glib::ScopedError error;
    if (cl->HasSwitch(switches::kAsyncSwitch)) {
      ClientLoop loop;
      loop.Initialize(&proxy);
      DBusGProxyCall* call =
          org_chromium_CryptohomeInterface_update_key_ex_async(
              proxy.gproxy(), account_ary.get(), auth_ary.get(), req_ary.get(),
              &ClientLoop::ParseReplyThunk, static_cast<gpointer>(&loop));
      if (!call)
        return 1;
      loop.Run();
      reply = loop.reply();
    } else {
      GArray* out_reply = NULL;
      if (!org_chromium_CryptohomeInterface_update_key_ex(
              proxy.gproxy(), account_ary.get(), auth_ary.get(), req_ary.get(),
              &out_reply, &brillo::Resetter(&error).lvalue())) {
        printf("Failed to call UpdateKeyEx!\n");
        return 1;
      }
      ParseBaseReply(out_reply, &reply, true /* print_reply */);
    }
    if (reply.has_error()) {
      printf("Key update failed.\n");
      return reply.error();
    }
    printf("Key updated.\n");
  } else if (!strcmp(switches::kActions[switches::ACTION_REMOVE],
                     action.c_str())) {
    std::string account_id;

    if (!GetAccountId(cl, &account_id)) {
      return 1;
    }

    if (!cl->HasSwitch(switches::kForceSwitch) && !ConfirmRemove(account_id)) {
      return 1;
    }

    cryptohome::AccountIdentifier identifier;
    identifier.set_account_id(account_id);

    brillo::glib::ScopedArray account_ary(GArrayFromProtoBuf(identifier));
    if (!account_ary.get())
      return 1;

    GArray* out_reply = nullptr;
    brillo::glib::ScopedError error;
    if (!org_chromium_CryptohomeInterface_remove_ex(
            proxy.gproxy(), account_ary.get(), &out_reply,
            &brillo::Resetter(&error).lvalue())) {
      printf("Remove call failed: %s.\n", error->message);
      return 1;
    }

    cryptohome::BaseReply reply;
    ParseBaseReply(out_reply, &reply, true /* print_reply */);
    if (reply.has_error()) {
      printf("Remove failed.\n");
      return 1;
    }
    printf("Remove succeeded.\n");
  } else if (!strcmp(switches::kActions[switches::ACTION_UNMOUNT],
                     action.c_str())) {
    cryptohome::UnmountRequest request;
    brillo::glib::ScopedArray request_ary(GArrayFromProtoBuf(request));
    if (!request_ary.get())
      return 1;

    GArray* out_reply = nullptr;
    brillo::glib::ScopedError error;
    if (!org_chromium_CryptohomeInterface_unmount_ex(
            proxy.gproxy(), request_ary.get(), &out_reply,
            &brillo::Resetter(&error).lvalue())) {
      printf("Unmount call failed: %s.\n", error->message);
      return 1;
    }

    cryptohome::BaseReply reply;
    ParseBaseReply(out_reply, &reply, true /* print_reply */);
    if (reply.has_error()) {
      printf("Unmount failed.\n");
      return 1;
    }
    printf("Unmount succeeded.\n");
  } else if (!strcmp(switches::kActions[switches::ACTION_MOUNTED],
                     action.c_str())) {
    brillo::glib::ScopedError error;
    gboolean done = false;
    if (!org_chromium_CryptohomeInterface_is_mounted(
            proxy.gproxy(), &done, &brillo::Resetter(&error).lvalue())) {
      printf("IsMounted call failed: %s.\n", error->message);
    }
    if (done) {
      printf("true\n");
    } else {
      printf("false\n");
    }
  } else if (!strcmp(switches::kActions[switches::ACTION_OBFUSCATE_USER],
                     action.c_str())) {
    std::string account_id;

    if (!GetAccountId(cl, &account_id)) {
      return 1;
    }

    if (cl->HasSwitch(switches::kUseDBus)) {
      gchar* result;
      brillo::glib::ScopedError error;
      if (!org_chromium_CryptohomeInterface_get_sanitized_username(
              proxy.gproxy(), account_id.c_str(), &result,
              &brillo::Resetter(&error).lvalue())) {
        printf("GetSanitizedUserName call failed: %s.\n", error->message);
        return 1;
      }
      printf("%s\n", result);
    } else {
      // Use libbrillo directly instead of going through dbus/cryptohome.
      if (!brillo::cryptohome::home::EnsureSystemSaltIsLoaded()) {
        printf("Failed to load system salt\n");
        return 1;
      }

      std::string* salt_ptr = brillo::cryptohome::home::GetSystemSalt();
      brillo::SecureBlob system_salt = SecureBlob(*salt_ptr);
      printf("%s\n", SanitizeUserNameWithSalt(account_id, system_salt).c_str());
    }
  } else if (!strcmp(switches::kActions[switches::ACTION_GET_SYSTEM_SALT],
                     action.c_str())) {
    brillo::SecureBlob system_salt;
    if (cl->HasSwitch(switches::kUseDBus)) {
      system_salt = GetSystemSalt(proxy);
      if (system_salt.empty()) {
        printf("Failed to retrieve system salt\n");
      }
    } else {
      // Use libbrillo directly instead of going through dbus/cryptohome.
      if (!brillo::cryptohome::home::EnsureSystemSaltIsLoaded()) {
        printf("Failed to load system salt\n");
        return 1;
      }

      std::string* salt_ptr = brillo::cryptohome::home::GetSystemSalt();
      system_salt = SecureBlob(*salt_ptr);
    }
    std::string hex_salt =
        base::HexEncode(system_salt.data(), system_salt.size());
    // We want to follow the convention of having low case hex for output as in
    // GetSanitizedUsername().
    std::transform(hex_salt.begin(), hex_salt.end(), hex_salt.begin(),
                   ::tolower);
    printf("%s\n", hex_salt.c_str());
  } else if (!strcmp(switches::kActions[switches::ACTION_DUMP_KEYSET],
                     action.c_str())) {
    std::string account_id;

    if (!GetAccountId(cl, &account_id)) {
      return 1;
    }

    FilePath vault_path =
        FilePath("/home/.shadow")
            .Append(SanitizeUserNameWithSalt(account_id, GetSystemSalt(proxy)))
            .Append("master.0");
    brillo::Blob contents;
    if (!platform.ReadFile(vault_path, &contents)) {
      printf("Couldn't load keyset contents: %s.\n",
             vault_path.value().c_str());
      return 1;
    }
    cryptohome::SerializedVaultKeyset serialized;
    if (!serialized.ParseFromArray(contents.data(), contents.size())) {
      printf("Couldn't parse keyset contents: %s.\n",
             vault_path.value().c_str());
      return 1;
    }
    printf("For keyset: %s\n", vault_path.value().c_str());
    printf("  Flags:\n");
    if ((serialized.flags() & cryptohome::SerializedVaultKeyset::TPM_WRAPPED) &&
        serialized.has_tpm_key()) {
      printf("    TPM_WRAPPED\n");
    }
    if ((serialized.flags() & cryptohome::SerializedVaultKeyset::PCR_BOUND) &&
        serialized.has_tpm_key() && serialized.has_extended_tpm_key()) {
      printf("    PCR_BOUND\n");
    }
    if (serialized.flags() &
        cryptohome::SerializedVaultKeyset::SCRYPT_WRAPPED) {
      printf("    SCRYPT_WRAPPED\n");
    }
    SecureBlob blob(serialized.salt().length());
    serialized.salt().copy(blob.char_data(), serialized.salt().length(), 0);
    printf("  Salt:\n");
    printf("    %s\n", cryptohome::CryptoLib::SecureBlobToHex(blob).c_str());
    blob.resize(serialized.wrapped_keyset().length());
    serialized.wrapped_keyset().copy(blob.char_data(),
                                     serialized.wrapped_keyset().length(), 0);
    printf("  Wrapped (Encrypted) Keyset:\n");
    printf("    %s\n", cryptohome::CryptoLib::SecureBlobToHex(blob).c_str());
    if (serialized.has_tpm_key()) {
      blob.resize(serialized.tpm_key().length());
      serialized.tpm_key().copy(blob.char_data(), serialized.tpm_key().length(),
                                0);
      printf("  TPM-Bound (Encrypted) Vault Encryption Key:\n");
      printf("    %s\n", cryptohome::CryptoLib::SecureBlobToHex(blob).c_str());
    }
    if (serialized.has_extended_tpm_key()) {
      blob.resize(serialized.extended_tpm_key().length());
      serialized.extended_tpm_key().copy(
          blob.char_data(), serialized.extended_tpm_key().length(), 0);
      printf("  TPM-Bound (Encrypted) Vault Encryption Key, PCR extended:\n");
      printf("    %s\n", cryptohome::CryptoLib::SecureBlobToHex(blob).c_str());
    }
    if (serialized.has_tpm_public_key_hash()) {
      blob.resize(serialized.tpm_public_key_hash().length());
      serialized.tpm_public_key_hash().copy(blob.char_data(),
                                            serialized.tpm_key().length(), 0);
      printf("  TPM Public Key Hash:\n");
      printf("    %s\n", cryptohome::CryptoLib::SecureBlobToHex(blob).c_str());
    }
    if (serialized.has_password_rounds()) {
      printf("  Password rounds:\n");
      printf("    %d\n", serialized.password_rounds());
    }
    if (serialized.has_timestamp_file_exists()) {
      FilePath timestamp_path = vault_path.AddExtension("timestamp");
      brillo::Blob tcontents;
      if (!platform.ReadFile(timestamp_path, &tcontents)) {
        printf("Couldn't load timestamp contents: %s.\n",
               timestamp_path.value().c_str());
      }
      cryptohome::Timestamp timestamp;
      if (!timestamp.ParseFromArray(tcontents.data(), tcontents.size())) {
        printf("Couldn't parse timestamp contents: %s.\n",
               timestamp_path.value().c_str());
      }
      const base::Time last_activity =
          base::Time::FromInternalValue(timestamp.timestamp());
      printf("  Last activity (days ago):\n");
      printf("    %d\n", (base::Time::Now() - last_activity).InDays());
    } else if (serialized.has_last_activity_timestamp()) {
      const base::Time last_activity =
          base::Time::FromInternalValue(serialized.last_activity_timestamp());
      printf("  Last activity (days ago):\n");
      printf("    %d\n", (base::Time::Now() - last_activity).InDays());
    }
  } else if (!strcmp(switches::kActions[switches::ACTION_DUMP_LAST_ACTIVITY],
                     action.c_str())) {
    std::vector<FilePath> user_dirs;
    if (!platform.EnumerateDirectoryEntries(FilePath("/home/.shadow/"), false,
                                            &user_dirs)) {
      LOG(ERROR) << "Can not list shadow root.";
      return 1;
    }
    for (std::vector<FilePath>::iterator it = user_dirs.begin();
         it != user_dirs.end(); ++it) {
      const std::string dir_name = it->BaseName().value();
      if (!brillo::cryptohome::home::IsSanitizedUserName(dir_name))
        continue;
      // TODO(wad): change it so that it uses GetVaultKeysets().
      std::unique_ptr<cryptohome::FileEnumerator> file_enumerator(
          platform.GetFileEnumerator(*it, false, base::FileEnumerator::FILES));
      base::Time max_activity = base::Time::UnixEpoch();
      FilePath next_path;
      while (!(next_path = file_enumerator->Next()).empty()) {
        FilePath file_name = next_path.BaseName().RemoveFinalExtension();
        // Scan for "master." files.
        if (file_name.value() != cryptohome::kKeyFile)
          continue;
        brillo::Blob contents;
        if (!platform.ReadFile(next_path, &contents)) {
          LOG(ERROR) << "Couldn't load keyset: " << next_path.value();
          continue;
        }
        cryptohome::SerializedVaultKeyset keyset;
        if (!keyset.ParseFromArray(contents.data(), contents.size())) {
          LOG(ERROR) << "Couldn't parse keyset: " << next_path.value();
          continue;
        }
        if (keyset.has_timestamp_file_exists()) {
          FilePath timestamp_path = next_path.AddExtension("timestamp");
          brillo::Blob tcontents;
          if (!platform.ReadFile(timestamp_path, &tcontents)) {
            printf("Couldn't load timestamp contents: %s.\n",
                   timestamp_path.value().c_str());
          }
          cryptohome::Timestamp timestamp;
          if (!timestamp.ParseFromArray(tcontents.data(), tcontents.size())) {
            printf("Couldn't parse timestamp contents: %s.\n",
                   timestamp_path.value().c_str());
          }
          const base::Time last_activity =
              base::Time::FromInternalValue(timestamp.timestamp());
          if (last_activity > max_activity) {
            max_activity = last_activity;
          }
        } else if (keyset.has_last_activity_timestamp()) {
          const base::Time last_activity =
              base::Time::FromInternalValue(keyset.last_activity_timestamp());
          if (last_activity > max_activity)
            max_activity = last_activity;
        }
      }
      if (max_activity > base::Time::UnixEpoch()) {
        printf("%s %3d\n", dir_name.c_str(),
               (base::Time::Now() - max_activity).InDays());
      }
    }
  } else if (!strcmp(switches::kActions[switches::ACTION_TPM_STATUS],
                     action.c_str())) {
    brillo::glib::ScopedError error;
    gboolean result = false;
    if (!org_chromium_CryptohomeInterface_tpm_is_enabled(
            proxy.gproxy(), &result, &brillo::Resetter(&error).lvalue())) {
      printf("TpmIsEnabled call failed: %s.\n", error->message);
    } else {
      printf("TPM Enabled: %s\n", (result ? "true" : "false"));
    }
    result = false;
    if (!org_chromium_CryptohomeInterface_tpm_is_owned(
            proxy.gproxy(), &result, &brillo::Resetter(&error).lvalue())) {
      printf("TpmIsOwned call failed: %s.\n", error->message);
    } else {
      printf("TPM Owned: %s\n", (result ? "true" : "false"));
    }
    if (!org_chromium_CryptohomeInterface_tpm_is_being_owned(
            proxy.gproxy(), &result, &brillo::Resetter(&error).lvalue())) {
      printf("TpmIsBeingOwned call failed: %s.\n", error->message);
    } else {
      printf("TPM Being Owned: %s\n", (result ? "true" : "false"));
    }
    if (!org_chromium_CryptohomeInterface_tpm_is_ready(
            proxy.gproxy(), &result, &brillo::Resetter(&error).lvalue())) {
      printf("TpmIsReady call failed: %s.\n", error->message);
    } else {
      printf("TPM Ready: %s\n", (result ? "true" : "false"));
    }
    gchar* password;
    if (!org_chromium_CryptohomeInterface_tpm_get_password(
            proxy.gproxy(), &password, &brillo::Resetter(&error).lvalue())) {
      printf("TpmGetPassword call failed: %s.\n", error->message);
    } else {
      printf("TPM Password: %s\n", password);
      g_free(password);
    }
  } else if (!strcmp(switches::kActions[switches::ACTION_TPM_MORE_STATUS],
                     action.c_str())) {
    cryptohome::GetTpmStatusRequest request;
    cryptohome::BaseReply reply;
    if (!MakeProtoDBusCall(cryptohome::kCryptohomeGetTpmStatus,
                           DBUS_METHOD(get_tpm_status),
                           DBUS_METHOD(get_tpm_status_async), cl, &proxy,
                           request, &reply, true /* print_reply */)) {
      return 1;
    }
    if (!reply.HasExtension(cryptohome::GetTpmStatusReply::reply)) {
      printf("GetTpmStatusReply missing.\n");
      return 1;
    }
    printf("GetTpmStatus success.\n");
  } else if (!strcmp(switches::kActions[switches::ACTION_STATUS],
                     action.c_str())) {
    brillo::glib::ScopedError error;
    gchar* status;
    if (!org_chromium_CryptohomeInterface_get_status_string(
            proxy.gproxy(), &status, &brillo::Resetter(&error).lvalue())) {
      printf("GetStatusString call failed: %s.\n", error->message);
    } else {
      printf("%s\n", status);
      g_free(status);
    }
  } else if (!strcmp(switches::kActions[switches::ACTION_SET_CURRENT_USER_OLD],
                     action.c_str())) {
    brillo::glib::ScopedError error;
    ClientLoop client_loop;
    client_loop.Initialize(&proxy);
    if (!org_chromium_CryptohomeInterface_update_current_user_activity_timestamp(  // NOLINT
            proxy.gproxy(),
            base::TimeDelta::FromDays(kSetCurrentUserOldOffsetInDays)
                .InSeconds(),
            &brillo::Resetter(&error).lvalue())) {
      printf("UpdateCurrentUserActivityTimestamp call failed: %s.\n",
             error->message);
    } else {
      printf(
          "Timestamp successfully updated. You may verify it with "
          "--action=dump_keyset --user=...\n");
    }
  } else if (!strcmp(switches::kActions[switches::ACTION_TPM_TAKE_OWNERSHIP],
                     action.c_str())) {
    brillo::glib::ScopedError error;
    if (!org_chromium_CryptohomeInterface_tpm_can_attempt_ownership(
            proxy.gproxy(), &brillo::Resetter(&error).lvalue())) {
      printf("TpmCanAttemptOwnership call failed: %s.\n", error->message);
    }
  } else if (!strcmp(
                 switches::kActions[switches::ACTION_TPM_CLEAR_STORED_PASSWORD],
                 action.c_str())) {
    brillo::glib::ScopedError error;
    if (!org_chromium_CryptohomeInterface_tpm_clear_stored_password(
            proxy.gproxy(), &brillo::Resetter(&error).lvalue())) {
      printf("TpmClearStoredPassword call failed: %s.\n", error->message);
    }
  } else if (!strcmp(
                 switches::kActions[switches::ACTION_INSTALL_ATTRIBUTES_GET],
                 action.c_str())) {
    std::string name;
    if (!GetAttrName(cl, &name)) {
      printf("No attribute name specified.\n");
      return 1;
    }

    brillo::glib::ScopedError error;
    gboolean result;
    if (!org_chromium_CryptohomeInterface_install_attributes_is_ready(
            proxy.gproxy(), &result, &brillo::Resetter(&error).lvalue())) {
      printf("IsReady call failed: %s.\n", error->message);
      return 1;
    }
    if (result == FALSE) {
      printf("InstallAttributes() is not ready.\n");
      return 1;
    }

    brillo::glib::ScopedArray value;
    if (!org_chromium_CryptohomeInterface_install_attributes_get(
            proxy.gproxy(), name.c_str(), &brillo::Resetter(&value).lvalue(),
            &result, &brillo::Resetter(&error).lvalue())) {
      printf("Get() failed: %s.\n", error->message);
      return 1;
    }
    std::string value_str(value->data, value->len);
    if (result == TRUE) {
      printf("%s\n", value_str.c_str());
    } else {
      return 1;
    }
  } else if (!strcmp(
                 switches::kActions[switches::ACTION_INSTALL_ATTRIBUTES_SET],
                 action.c_str())) {
    std::string name;
    if (!GetAttrName(cl, &name)) {
      printf("No attribute name specified.\n");
      return 1;
    }
    std::string value;
    if (!GetAttrValue(cl, &value)) {
      printf("No attribute value specified.\n");
      return 1;
    }

    brillo::glib::ScopedError error;
    gboolean result;
    if (!org_chromium_CryptohomeInterface_install_attributes_is_ready(
            proxy.gproxy(), &result, &brillo::Resetter(&error).lvalue())) {
      printf("IsReady call failed: %s.\n", error->message);
      return 1;
    }

    if (result == FALSE) {
      printf("InstallAttributes() is not ready.\n");
      return 1;
    }

    brillo::glib::ScopedArray value_ary(g_array_new(FALSE, FALSE, 1));
    g_array_append_vals(value_ary.get(), value.c_str(), value.size() + 1);
    if (!org_chromium_CryptohomeInterface_install_attributes_set(
            proxy.gproxy(), name.c_str(), value_ary.get(), &result,
            &brillo::Resetter(&error).lvalue())) {
      printf("Set() failed: %s.\n", error->message);
      return 1;
    }
    if (result == FALSE)
      return 1;
  } else if (!strcmp(switches::kActions
                         [switches::ACTION_INSTALL_ATTRIBUTES_FINALIZE],
                     action.c_str())) {
    brillo::glib::ScopedError error;
    gboolean result;
    if (!org_chromium_CryptohomeInterface_install_attributes_is_ready(
            proxy.gproxy(), &result, &brillo::Resetter(&error).lvalue())) {
      printf("IsReady call failed: %s.\n", error->message);
      return 1;
    }
    if (result == FALSE) {
      printf("InstallAttributes is not ready.\n");
      return 1;
    }
    if (!org_chromium_CryptohomeInterface_install_attributes_finalize(
            proxy.gproxy(), &result, &brillo::Resetter(&error).lvalue())) {
      printf("Finalize() failed: %s.\n", error->message);
      return 1;
    }
    printf("InstallAttributesFinalize(): %d\n", result);
  } else if (!strcmp(
                 switches::kActions[switches::ACTION_INSTALL_ATTRIBUTES_COUNT],
                 action.c_str())) {
    brillo::glib::ScopedError error;
    gint result;
    if (!org_chromium_CryptohomeInterface_install_attributes_count(
            proxy.gproxy(), &result, &brillo::Resetter(&error).lvalue())) {
      printf("InstallAttributesCount() call failed: %s.\n", error->message);
      return 1;
    }
    printf("InstallAttributesCount(): %d\n", result);
  } else if (!strcmp(switches::kActions
                         [switches::ACTION_INSTALL_ATTRIBUTES_IS_READY],
                     action.c_str())) {
    brillo::glib::ScopedError error;
    gboolean result;
    if (!org_chromium_CryptohomeInterface_install_attributes_is_ready(
            proxy.gproxy(), &result, &brillo::Resetter(&error).lvalue())) {
      printf("InstallAttributesIsReady() call failed: %s.\n", error->message);
      return 1;
    }
    printf("InstallAttributesIsReady(): %d\n", static_cast<int>(result));
  } else if (!strcmp(switches::kActions
                         [switches::ACTION_INSTALL_ATTRIBUTES_IS_SECURE],
                     action.c_str())) {
    brillo::glib::ScopedError error;
    gboolean result;
    if (!org_chromium_CryptohomeInterface_install_attributes_is_secure(
            proxy.gproxy(), &result, &brillo::Resetter(&error).lvalue())) {
      printf("InstallAttributesIsSecure() call failed: %s.\n", error->message);
      return 1;
    }
    printf("InstallAttributesIsSecure(): %d\n", static_cast<int>(result));
  } else if (!strcmp(switches::kActions
                         [switches::ACTION_INSTALL_ATTRIBUTES_IS_INVALID],
                     action.c_str())) {
    brillo::glib::ScopedError error;
    gboolean result;
    if (!org_chromium_CryptohomeInterface_install_attributes_is_invalid(
            proxy.gproxy(), &result, &brillo::Resetter(&error).lvalue())) {
      printf("InstallAttributesIsInvalid() call failed: %s.\n", error->message);
      return 1;
    }
    printf("InstallAttributesIsInvalid(): %d\n", static_cast<int>(result));
  } else if (!strcmp(switches::kActions
                         [switches::ACTION_INSTALL_ATTRIBUTES_IS_FIRST_INSTALL],
                     action.c_str())) {
    brillo::glib::ScopedError error;
    gboolean result;
    if (!org_chromium_CryptohomeInterface_install_attributes_is_first_install(
            proxy.gproxy(), &result, &brillo::Resetter(&error).lvalue())) {
      printf("InstallAttributesIsFirstInstall() call failed: %s.\n",
             error->message);
      return 1;
    }
    printf("InstallAttributesIsFirstInstall(): %d\n", static_cast<int>(result));
  } else if (!strcmp(switches::kActions[switches::ACTION_TPM_WAIT_OWNERSHIP],
                     action.c_str())) {
    // Note that this is a rather hackish implementation that will be replaced
    // once the refactor to distributed mode is over. It'll be replaced with an
    // implementation that does one synchronous call to tpm_manager's
    // TakeOwnership(), then check if it's owned.
    int timeout = kWaitOwnershipTimeoutInSeconds;
    int timeout_in_switch;
    if (cl->HasSwitch(switches::kWaitOwnershipTimeoutSwitch) &&
        base::StringToInt(
            cl->GetSwitchValueASCII(switches::kWaitOwnershipTimeoutSwitch),
            &timeout_in_switch)) {
      timeout = timeout_in_switch;
    }

    brillo::glib::ScopedError error;
    auto deadline = base::Time::Now() + base::TimeDelta::FromSeconds(
                                            kWaitOwnershipTimeoutInSeconds);
    while (base::Time::Now() < deadline) {
      base::PlatformThread::Sleep(
          base::TimeDelta::FromMilliseconds(kWaitOwnershipPollIntervalInMs));
      gboolean is_owned = false;
      if (!org_chromium_CryptohomeInterface_tpm_is_owned(
              proxy.gproxy(), &is_owned, &brillo::Resetter(&error).lvalue())) {
        printf("TpmIsOwned call failed: %s.\n", error->message);
        return 1;
      }
      if (is_owned) {
        // This is the condition we are waiting for.
        printf("TPM is now owned.\n");
        return 0;
      }
    }
    printf("Fail to own TPM.\n");
    return 1;
  } else if (!strcmp(switches::kActions
                         [switches::ACTION_PKCS11_GET_USER_TOKEN_INFO],
                     action.c_str())) {
    // If no account_id is specified, proceed with the empty string.
    std::string account_id = cl->GetSwitchValueASCII(switches::kUserSwitch);
    if (!account_id.empty()) {
      brillo::glib::ScopedError error;
      gchar* label = NULL;
      gchar* pin = NULL;
      int slot = 0;
      if (!org_chromium_CryptohomeInterface_pkcs11_get_tpm_token_info_for_user(
              proxy.gproxy(), account_id.c_str(), &label, &pin, &slot,
              &brillo::Resetter(&error).lvalue())) {
        printf("PKCS #11 info call failed: %s.\n", error->message);
      } else {
        printf("Token properties for %s:\n", account_id.c_str());
        printf("Label = %s\n", label);
        printf("Pin = %s\n", pin);
        printf("Slot = %d\n", slot);
        g_free(label);
        g_free(pin);
      }
    } else {
      printf("Account ID/Username not specified.\n");
      return 1;
    }
  } else if (!strcmp(switches::kActions
                         [switches::ACTION_PKCS11_GET_SYSTEM_TOKEN_INFO],
                     action.c_str())) {
    brillo::glib::ScopedError error;
    gchar* label = NULL;
    gchar* pin = NULL;
    int slot = 0;
    if (!org_chromium_CryptohomeInterface_pkcs11_get_tpm_token_info(
            proxy.gproxy(), &label, &pin, &slot,
            &brillo::Resetter(&error).lvalue())) {
      printf("PKCS #11 info call failed: %s.\n", error->message);
    } else {
      printf("System token properties:\n");
      printf("Label = %s\n", label);
      printf("Pin = %s\n", pin);
      printf("Slot = %d\n", slot);
      g_free(label);
      g_free(pin);
    }
  } else if (!strcmp(
                 switches::kActions[switches::ACTION_PKCS11_IS_USER_TOKEN_OK],
                 action.c_str())) {
    cryptohome::Pkcs11Init init;
    if (!init.IsUserTokenOK()) {
      printf("User token looks broken!\n");
      return 1;
    }
    printf("User token looks OK!\n");
  } else if (!strcmp(switches::kActions[switches::ACTION_PKCS11_TERMINATE],
                     action.c_str())) {
    // If no account_id is specified, proceed with the empty string.
    std::string account_id;
    GetAccountId(cl, &account_id);
    brillo::glib::ScopedError error;
    if (!org_chromium_CryptohomeInterface_pkcs11_terminate(
            proxy.gproxy(), account_id.c_str(),
            &brillo::Resetter(&error).lvalue())) {
      printf("PKCS #11 terminate call failed: %s.\n", error->message);
    }
  } else if (!strcmp(
                 switches::kActions[switches::ACTION_TPM_VERIFY_ATTESTATION],
                 action.c_str())) {
    bool is_cros_core = cl->HasSwitch(switches::kCrosCoreSwitch);
    brillo::glib::ScopedError error;
    gboolean result = FALSE;
    if (!org_chromium_CryptohomeInterface_tpm_verify_attestation_data(
            proxy.gproxy(), is_cros_core, &result,
            &brillo::Resetter(&error).lvalue())) {
      printf("TpmVerifyAttestationData call failed: %s.\n", error->message);
      return 1;
    }
    if (result == FALSE) {
      printf("TPM attestation data is not valid or is not available.\n");
      return 1;
    }
  } else if (!strcmp(switches::kActions[switches::ACTION_TPM_VERIFY_EK],
                     action.c_str())) {
    bool is_cros_core = cl->HasSwitch(switches::kCrosCoreSwitch);
    brillo::glib::ScopedError error;
    gboolean result = FALSE;
    if (!org_chromium_CryptohomeInterface_tpm_verify_ek(
            proxy.gproxy(), is_cros_core, &result,
            &brillo::Resetter(&error).lvalue())) {
      printf("TpmVerifyEK call failed: %s.\n", error->message);
      return 1;
    }
    if (result == FALSE) {
      printf("TPM endorsement key is not valid or is not available.\n");
      return 1;
    }
  } else if (!strcmp(
                 switches::kActions[switches::ACTION_TPM_ATTESTATION_STATUS],
                 action.c_str())) {
    brillo::glib::ScopedError error;
    gboolean result = FALSE;
    if (!org_chromium_CryptohomeInterface_tpm_is_attestation_prepared(
            proxy.gproxy(), &result, &brillo::Resetter(&error).lvalue())) {
      printf("TpmIsAttestationPrepared call failed: %s.\n", error->message);
    } else {
      printf("Attestation Prepared: %s\n", (result ? "true" : "false"));
    }
    if (!org_chromium_CryptohomeInterface_tpm_is_attestation_enrolled(
            proxy.gproxy(), &result, &brillo::Resetter(&error).lvalue())) {
      printf("TpmIsAttestationEnrolled call failed: %s.\n", error->message);
    } else {
      printf("Attestation Enrolled: %s\n", (result ? "true" : "false"));
    }
  } else if (!strcmp(switches::kActions
                         [switches::ACTION_TPM_ATTESTATION_MORE_STATUS],
                     action.c_str())) {
    cryptohome::AttestationGetEnrollmentPreparationsRequest request;
    cryptohome::BaseReply reply;
    if (!MakeProtoDBusCall(
            cryptohome::kCryptohomeTpmAttestationGetEnrollmentPreparationsEx,
            DBUS_METHOD(tpm_attestation_get_enrollment_preparations_ex),
            DBUS_METHOD(tpm_attestation_get_enrollment_preparations_ex_async),
            cl, &proxy, request, &reply, false /* print_reply */)) {
      printf("TpmAttestationGetEnrollmentPreparationsEx call failed.\n");
    } else if (!reply.HasExtension(
                   cryptohome::AttestationGetEnrollmentPreparationsReply::
                       reply)) {
      printf("AttestationGetEnrollmentPreparationsReply missing.\n");
    } else {
      cryptohome::AttestationGetEnrollmentPreparationsReply* extension =
          reply.MutableExtension(
              cryptohome::AttestationGetEnrollmentPreparationsReply::reply);
      auto map = extension->enrollment_preparations();
      bool prepared = false;
      for (auto it = map.cbegin(), end = map.cend(); it != end; ++it) {
        prepared |= it->second;
      }
      printf("Attestation Prepared: %s\n", prepared ? "true" : "false");
      for (auto it = map.cbegin(), end = map.cend(); it != end; ++it) {
        printf("    Prepared for %s: %s\n", GetPCAName(it->first).c_str(),
               (it->second ? "true" : "false"));
      }
    }
    // TODO(crbug.com/922062): Replace with a call listing all identity certs.
    brillo::glib::ScopedError error;
    gboolean result = FALSE;
    if (!org_chromium_CryptohomeInterface_tpm_is_attestation_enrolled(
            proxy.gproxy(), &result, &brillo::Resetter(&error).lvalue())) {
      printf("TpmIsAttestationEnrolled call failed: %s.\n", error->message);
    } else {
      printf("Attestation Enrolled: %s\n", (result ? "true" : "false"));
    }
  } else if (!strcmp(switches::kActions
                         [switches::ACTION_TPM_ATTESTATION_START_ENROLL],
                     action.c_str())) {
    brillo::glib::ScopedError error;
    std::string response_data;
    if (!cl->HasSwitch(switches::kAsyncSwitch)) {
      brillo::glib::ScopedArray data;
      if (!org_chromium_CryptohomeInterface_tpm_attestation_create_enroll_request(  // NOLINT
              proxy.gproxy(), pca_type, &brillo::Resetter(&data).lvalue(),
              &brillo::Resetter(&error).lvalue())) {
        printf("TpmAttestationCreateEnrollRequest call failed: %s.\n",
               error->message);
        return 1;
      }
      response_data = std::string(static_cast<char*>(data->data), data->len);
    } else {
      ClientLoop client_loop;
      client_loop.Initialize(&proxy);
      gint async_id = -1;
      if (!org_chromium_CryptohomeInterface_async_tpm_attestation_create_enroll_request(  // NOLINT
              proxy.gproxy(), pca_type, &async_id,
              &brillo::Resetter(&error).lvalue())) {
        printf("AsyncTpmAttestationCreateEnrollRequest call failed: %s.\n",
               error->message);
        return 1;
      } else {
        client_loop.Run(async_id);
        if (!client_loop.get_return_status()) {
          printf("Attestation enrollment request failed.\n");
          return 1;
        }
        response_data = client_loop.get_return_data();
      }
    }
    base::WriteFile(GetOutputFile(cl), response_data.data(),
                    response_data.length());
  } else if (!strcmp(switches::kActions
                         [switches::ACTION_TPM_ATTESTATION_FINISH_ENROLL],
                     action.c_str())) {
    std::string contents;
    if (!base::ReadFileToString(GetInputFile(cl), &contents)) {
      printf("Failed to read input file.\n");
      return 1;
    }
    brillo::glib::ScopedArray data(g_array_new(FALSE, FALSE, 1));
    g_array_append_vals(data.get(), contents.data(), contents.length());
    gboolean success = FALSE;
    brillo::glib::ScopedError error;
    if (!cl->HasSwitch(switches::kAsyncSwitch)) {
      if (!org_chromium_CryptohomeInterface_tpm_attestation_enroll(
              proxy.gproxy(), pca_type, data.get(), &success,
              &brillo::Resetter(&error).lvalue())) {
        printf("TpmAttestationEnroll call failed: %s.\n", error->message);
        return 1;
      }
    } else {
      ClientLoop client_loop;
      client_loop.Initialize(&proxy);
      gint async_id = -1;
      if (!org_chromium_CryptohomeInterface_async_tpm_attestation_enroll(
              proxy.gproxy(), pca_type, data.get(), &async_id,
              &brillo::Resetter(&error).lvalue())) {
        printf("AsyncTpmAttestationEnroll call failed: %s.\n", error->message);
        return 1;
      } else {
        client_loop.Run(async_id);
        success = client_loop.get_return_status();
      }
    }
    if (!success) {
      printf("Attestation enrollment failed.\n");
      return 1;
    }
  } else if (!strcmp(
                 switches::kActions[switches::ACTION_TPM_ATTESTATION_ENROLL],
                 action.c_str())) {
    brillo::glib::ScopedError error;
    gboolean success = FALSE;
    const bool forced = cl->HasSwitch(switches::kForceSwitch);
    if (!cl->HasSwitch(switches::kAsyncSwitch)) {
      brillo::glib::ScopedArray data;
      if (!org_chromium_CryptohomeInterface_tpm_attestation_enroll_ex(
              proxy.gproxy(), pca_type, forced, &success,
              &brillo::Resetter(&error).lvalue())) {
        printf("TpmAttestationEnrollEx call failed: %s.\n", error->message);
        return 1;
      }
    } else {
      ClientLoop client_loop;
      client_loop.Initialize(&proxy);
      gint async_id = -1;
      if (!org_chromium_CryptohomeInterface_async_tpm_attestation_enroll_ex(
              proxy.gproxy(), pca_type, forced, &async_id,
              &brillo::Resetter(&error).lvalue())) {
        printf("AsyncTpmAttestationEnrollEx call failed: %s.\n",
               error->message);
        return 1;
      } else {
        client_loop.Run(async_id);
        success = client_loop.get_return_status();
      }
    }
    if (!success) {
      printf("Attestation enrollment failed.\n");
      return 1;
    }
  } else if (!strcmp(switches::kActions
                         [switches::ACTION_TPM_ATTESTATION_START_CERTREQ],
                     action.c_str())) {
    brillo::glib::ScopedError error;
    std::string response_data;
    cryptohome::CertificateProfile profile;
    if (!GetProfile(cl, &profile)) {
      return 1;
    }
    if (!cl->HasSwitch(switches::kAsyncSwitch)) {
      brillo::glib::ScopedArray data;
      if (!org_chromium_CryptohomeInterface_tpm_attestation_create_cert_request(
              proxy.gproxy(), pca_type, profile, "", "",
              &brillo::Resetter(&data).lvalue(),
              &brillo::Resetter(&error).lvalue())) {
        printf("TpmAttestationCreateCertRequest call failed: %s.\n",
               error->message);
        return 1;
      }
      response_data = std::string(static_cast<char*>(data->data), data->len);
    } else {
      ClientLoop client_loop;
      client_loop.Initialize(&proxy);
      gint async_id = -1;
      if (!org_chromium_CryptohomeInterface_async_tpm_attestation_create_cert_request(  // NOLINT
              proxy.gproxy(), pca_type, profile, "", "", &async_id,
              &brillo::Resetter(&error).lvalue())) {
        printf("AsyncTpmAttestationCreateCertRequest call failed: %s.\n",
               error->message);
        return 1;
      } else {
        client_loop.Run(async_id);
        if (!client_loop.get_return_status()) {
          printf("Attestation certificate request failed.\n");
          return 1;
        }
        response_data = client_loop.get_return_data();
      }
    }
    base::WriteFile(GetOutputFile(cl), response_data.data(),
                    response_data.length());
  } else if (!strcmp(switches::kActions
                         [switches::ACTION_TPM_ATTESTATION_FINISH_CERTREQ],
                     action.c_str())) {
    std::string account_id = cl->GetSwitchValueASCII(switches::kUserSwitch);
    std::string key_name = cl->GetSwitchValueASCII(switches::kAttrNameSwitch);
    if (key_name.length() == 0) {
      printf("No key name specified (--%s=<name>)\n",
             switches::kAttrNameSwitch);
      return 1;
    }
    std::string contents;
    if (!base::ReadFileToString(GetInputFile(cl), &contents)) {
      printf("Failed to read input file.\n");
      return 1;
    }
    gboolean is_user_specific = !account_id.empty();
    brillo::glib::ScopedArray data(g_array_new(FALSE, FALSE, 1));
    g_array_append_vals(data.get(), contents.data(), contents.length());
    gboolean success = FALSE;
    brillo::glib::ScopedError error;
    std::string cert_data;
    if (!cl->HasSwitch(switches::kAsyncSwitch)) {
      brillo::glib::ScopedArray cert;
      if (!org_chromium_CryptohomeInterface_tpm_attestation_finish_cert_request(
              proxy.gproxy(), data.get(), is_user_specific, account_id.c_str(),
              key_name.c_str(), &brillo::Resetter(&cert).lvalue(), &success,
              &brillo::Resetter(&error).lvalue())) {
        printf("TpmAttestationFinishCertRequest call failed: %s.\n",
               error->message);
        return 1;
      }
      cert_data = std::string(static_cast<char*>(cert->data), cert->len);
    } else {
      ClientLoop client_loop;
      client_loop.Initialize(&proxy);
      gint async_id = -1;
      if (!org_chromium_CryptohomeInterface_async_tpm_attestation_finish_cert_request(  // NOLINT
              proxy.gproxy(), data.get(), is_user_specific, account_id.c_str(),
              key_name.c_str(), &async_id,
              &brillo::Resetter(&error).lvalue())) {
        printf("AsyncTpmAttestationFinishCertRequest call failed: %s.\n",
               error->message);
        return 1;
      } else {
        client_loop.Run(async_id);
        success = client_loop.get_return_status();
        cert_data = client_loop.get_return_data();
      }
    }
    if (!success) {
      printf("Attestation certificate request failed.\n");
      return 1;
    }
    base::WriteFile(GetOutputFile(cl), cert_data.data(), cert_data.length());
  } else if (!strcmp(switches::kActions
                         [switches::ACTION_TPM_ATTESTATION_GET_CERTIFICATE],
                     action.c_str())) {
    brillo::glib::ScopedError error;
    cryptohome::CertificateProfile profile;
    const std::string account_id =
        cl->GetSwitchValueASCII(switches::kUserSwitch);
    const std::string key_name =
        cl->GetSwitchValueASCII(switches::kAttrNameSwitch);
    const bool forced = cl->HasSwitch(switches::kForceSwitch);
    const bool shall_trigger_enrollment =
        cl->HasSwitch(switches::kEnrollSwitch);

    gboolean success = FALSE;
    std::string cert;
    if (!GetProfile(cl, &profile)) {
      return 1;
    }
    if (!cl->HasSwitch(switches::kAsyncSwitch)) {
      brillo::glib::ScopedArray data;
      if (!org_chromium_CryptohomeInterface_tpm_attestation_get_certificate_ex(
              proxy.gproxy(), profile, account_id.c_str(),
              /*request_origin=*/"", pca_type,
              /*key_type=*/1, key_name.c_str(), forced,
              shall_trigger_enrollment, &brillo::Resetter(&data).lvalue(),
              &success, &brillo::Resetter(&error).lvalue())) {
        printf("TpmAttestationCreateCertRequest call failed: %s.\n",
               error->message);
        return 1;
      }
      cert = std::string(static_cast<char*>(data->data), data->len);
    } else {
      ClientLoop client_loop;
      client_loop.Initialize(&proxy);
      gint async_id = -1;
      if (!org_chromium_CryptohomeInterface_async_tpm_attestation_get_certificate_ex(  // NOLINT
              proxy.gproxy(), profile, account_id.c_str(),
              /*request_origin=*/"", pca_type,
              /*key_type=*/1, key_name.c_str(), forced,
              shall_trigger_enrollment, &async_id,
              &brillo::Resetter(&error).lvalue())) {
        printf("AsyncTpmAttestationCreateCertRequest call failed: %s.\n",
               error->message);
        return 1;
      } else {
        client_loop.Run(async_id);
        success = client_loop.get_return_status();
        cert = client_loop.get_return_data();
      }
    }
    if (!success) {
      printf("Attestation certificate request failed.\n");
      return 1;
    }
    base::WriteFile(GetOutputFile(cl), cert.data(), cert.length());
  } else if (!strcmp(switches::kActions
                         [switches::ACTION_TPM_ATTESTATION_KEY_STATUS],
                     action.c_str())) {
    std::string account_id = cl->GetSwitchValueASCII(switches::kUserSwitch);
    std::string key_name = cl->GetSwitchValueASCII(switches::kAttrNameSwitch);
    if (key_name.length() == 0) {
      printf("No key name specified (--%s=<name>)\n",
             switches::kAttrNameSwitch);
      return 1;
    }
    gboolean is_user_specific = !account_id.empty();
    brillo::glib::ScopedError error;
    gboolean exists = FALSE;
    if (!org_chromium_CryptohomeInterface_tpm_attestation_does_key_exist(
            proxy.gproxy(), is_user_specific, account_id.c_str(),
            key_name.c_str(), &exists, &brillo::Resetter(&error).lvalue())) {
      printf("TpmAttestationDoesKeyExist call failed: %s.\n", error->message);
      return 1;
    }
    if (!exists) {
      printf("Key does not exist.\n");
      return 0;
    }
    gboolean success = FALSE;
    brillo::glib::ScopedArray cert;
    if (!org_chromium_CryptohomeInterface_tpm_attestation_get_certificate(
            proxy.gproxy(), is_user_specific, account_id.c_str(),
            key_name.c_str(), &brillo::Resetter(&cert).lvalue(), &success,
            &brillo::Resetter(&error).lvalue())) {
      printf("TpmAttestationGetCertificate call failed: %s.\n", error->message);
      return 1;
    }
    brillo::glib::ScopedArray public_key;
    if (!org_chromium_CryptohomeInterface_tpm_attestation_get_public_key(
            proxy.gproxy(), is_user_specific, account_id.c_str(),
            key_name.c_str(), &brillo::Resetter(&public_key).lvalue(), &success,
            &brillo::Resetter(&error).lvalue())) {
      printf("TpmAttestationGetPublicKey call failed: %s.\n", error->message);
      return 1;
    }
    std::string cert_pem =
        std::string(static_cast<char*>(cert->data), cert->len);
    std::string public_key_hex =
        base::HexEncode(public_key->data, public_key->len);
    printf("Public Key:\n%s\n\nCertificate:\n%s\n", public_key_hex.c_str(),
           cert_pem.c_str());
  } else if (!strcmp(switches::kActions
                         [switches::ACTION_TPM_ATTESTATION_REGISTER_KEY],
                     action.c_str())) {
    std::string account_id = cl->GetSwitchValueASCII(switches::kUserSwitch);
    std::string key_name = cl->GetSwitchValueASCII(switches::kAttrNameSwitch);
    if (key_name.length() == 0) {
      printf("No key name specified (--%s=<name>)\n",
             switches::kAttrNameSwitch);
      return 1;
    }
    const gboolean is_user_specific = !account_id.empty();
    ClientLoop client_loop;
    client_loop.Initialize(&proxy);
    gint async_id = -1;
    brillo::glib::ScopedError error;
    if (!org_chromium_CryptohomeInterface_tpm_attestation_register_key(
            proxy.gproxy(), is_user_specific, account_id.c_str(),
            key_name.c_str(), &async_id, &brillo::Resetter(&error).lvalue())) {
      printf("TpmAttestationRegisterKey call failed: %s.\n", error->message);
      return 1;
    } else {
      client_loop.Run(async_id);
      gboolean result = client_loop.get_return_status();
      printf("Result: %s\n", result ? "Success" : "Failure");
    }
  } else if (!strcmp(
                 switches::kActions
                     [switches::ACTION_TPM_ATTESTATION_ENTERPRISE_CHALLENGE],
                 action.c_str())) {
    std::string account_id = cl->GetSwitchValueASCII(switches::kUserSwitch);
    std::string key_name = cl->GetSwitchValueASCII(switches::kAttrNameSwitch);
    if (key_name.length() == 0) {
      printf("No key name specified (--%s=<name>)\n",
             switches::kAttrNameSwitch);
      return 1;
    }
    gboolean is_user_specific = !account_id.empty();
    std::string contents;
    if (!base::ReadFileToString(GetInputFile(cl), &contents)) {
      printf("Failed to read input file: %s\n",
             GetInputFile(cl).value().c_str());
      return 1;
    }
    brillo::glib::ScopedArray challenge(g_array_new(FALSE, FALSE, 1));
    g_array_append_vals(challenge.get(), contents.data(), contents.length());
    brillo::glib::ScopedArray device_id(g_array_new(FALSE, FALSE, 1));
    std::string device_id_str = "fake_device_id";
    g_array_append_vals(device_id.get(), device_id_str.data(),
                        device_id_str.length());
    brillo::glib::ScopedError error;
    ClientLoop client_loop;
    client_loop.Initialize(&proxy);
    gint async_id = -1;
    if (!org_chromium_CryptohomeInterface_tpm_attestation_sign_enterprise_va_challenge(  // NOLINT
            proxy.gproxy(), va_type, is_user_specific, account_id.c_str(),
            key_name.c_str(), account_id.c_str(), device_id.get(), TRUE,
            challenge.get(), &async_id, &brillo::Resetter(&error).lvalue())) {
      printf("AsyncTpmAttestationSignEnterpriseVaChallenge call failed: %s.\n",
             error->message);
      return 1;
    }
    client_loop.Run(async_id);
    if (!client_loop.get_return_status()) {
      printf("Attestation challenge response failed.\n");
      return 1;
    }
    std::string response_data = client_loop.get_return_data();
    base::WriteFileDescriptor(STDOUT_FILENO, response_data.data(),
                              response_data.length());
  } else if (!strcmp(switches::kActions
                         [switches::ACTION_TPM_ATTESTATION_SIMPLE_CHALLENGE],
                     action.c_str())) {
    std::string account_id = cl->GetSwitchValueASCII(switches::kUserSwitch);
    std::string key_name = cl->GetSwitchValueASCII(switches::kAttrNameSwitch);
    if (key_name.length() == 0) {
      printf("No key name specified (--%s=<name>)\n",
             switches::kAttrNameSwitch);
      return 1;
    }
    gboolean is_user_specific = !account_id.empty();
    std::string contents = "challenge";
    brillo::glib::ScopedArray challenge(g_array_new(FALSE, FALSE, 1));
    g_array_append_vals(challenge.get(), contents.data(), contents.length());
    brillo::glib::ScopedError error;
    ClientLoop client_loop;
    client_loop.Initialize(&proxy);
    gint async_id = -1;
    if (!org_chromium_CryptohomeInterface_tpm_attestation_sign_simple_challenge(
            proxy.gproxy(), is_user_specific, account_id.c_str(),
            key_name.c_str(), challenge.get(), &async_id,
            &brillo::Resetter(&error).lvalue())) {
      printf("AsyncTpmAttestationSignSimpleChallenge call failed: %s.\n",
             error->message);
      return 1;
    }
    client_loop.Run(async_id);
    if (!client_loop.get_return_status()) {
      printf("Attestation challenge response failed.\n");
      return 1;
    }
    std::string response_data = client_loop.get_return_data();
    base::WriteFileDescriptor(STDOUT_FILENO, response_data.data(),
                              response_data.length());
  } else if (!strcmp(switches::kActions
                         [switches::ACTION_TPM_ATTESTATION_GET_KEY_PAYLOAD],
                     action.c_str())) {
    std::string account_id = cl->GetSwitchValueASCII(switches::kUserSwitch);
    std::string key_name = cl->GetSwitchValueASCII(switches::kAttrNameSwitch);
    if (key_name.length() == 0) {
      printf("No key name specified (--%s=<name>)\n",
             switches::kAttrNameSwitch);
      return 1;
    }
    brillo::glib::ScopedArray payload;
    gboolean is_user_specific = !account_id.empty();
    brillo::glib::ScopedError error;
    gboolean success = FALSE;
    if (!org_chromium_CryptohomeInterface_tpm_attestation_get_key_payload(
            proxy.gproxy(), is_user_specific, account_id.c_str(),
            key_name.c_str(), &brillo::Resetter(&payload).lvalue(), &success,
            &brillo::Resetter(&error).lvalue())) {
      printf("AsyncTpmAttestationGetKetPayload call failed: %s.\n",
             error->message);
      return 1;
    }
    if (!success) {
      printf("AsyncTpmAttestationGetKetPayload operation failed.\n");
      return 1;
    }
    base::WriteFile(GetOutputFile(cl), payload->data, payload->len);
    base::WriteFileDescriptor(STDOUT_FILENO, payload->data, payload->len);
  } else if (!strcmp(switches::kActions
                         [switches::ACTION_TPM_ATTESTATION_SET_KEY_PAYLOAD],
                     action.c_str())) {
    std::string account_id = cl->GetSwitchValueASCII(switches::kUserSwitch);
    std::string key_name = cl->GetSwitchValueASCII(switches::kAttrNameSwitch);
    std::string value = cl->GetSwitchValueASCII(switches::kAttrValueSwitch);
    if (key_name.length() == 0) {
      printf("No key name specified (--%s=<name>)\n",
             switches::kAttrNameSwitch);
      return 1;
    }
    if (value.length() == 0) {
      printf("No payload specified (--%s=<payload>)\n",
             switches::kAttrValueSwitch);
      return 1;
    }
    brillo::glib::ScopedArray payload(g_array_new(FALSE, FALSE, 1));
    g_array_append_vals(payload.get(), value.data(), value.length());
    gboolean is_user_specific = !account_id.empty();
    brillo::glib::ScopedError error;
    gboolean success = FALSE;
    if (!org_chromium_CryptohomeInterface_tpm_attestation_set_key_payload(
            proxy.gproxy(), is_user_specific, account_id.c_str(),
            key_name.c_str(), payload.get(), &success,
            &brillo::Resetter(&error).lvalue())) {
      printf("AsyncTpmAttestationSetKetPayload call failed: %s.\n",
             error->message);
      return 1;
    }
    if (!success) {
      printf("AsyncTpmAttestationSetKetPayload operation failed.\n");
      return 1;
    }
  } else if (!strcmp(switches::kActions
                         [switches::ACTION_TPM_ATTESTATION_DELETE_KEYS],
                     action.c_str())) {
    std::string account_id = cl->GetSwitchValueASCII(switches::kUserSwitch);
    std::string key_prefix =
        cl->GetSwitchValueASCII(switches::kAttrPrefixSwitch);
    if (key_prefix.empty()) {
      printf("No key prefix specified (--%s=<prefix>)\n",
             switches::kAttrPrefixSwitch);
      return 1;
    }
    gboolean is_user_specific = !account_id.empty();
    brillo::glib::ScopedError error;
    gboolean success = FALSE;
    if (!org_chromium_CryptohomeInterface_tpm_attestation_delete_keys(
            proxy.gproxy(), is_user_specific, account_id.c_str(),
            key_prefix.c_str(), &success, &brillo::Resetter(&error).lvalue())) {
      printf("AsyncTpmAttestationDeleteKeys call failed: %s.\n",
             error->message);
      return 1;
    }
    if (!success) {
      printf("Delete operation failed.\n");
      return 1;
    }
  } else if (!strcmp(switches::kActions
                         [switches::ACTION_TPM_ATTESTATION_DELETE_KEY],
                     action.c_str())) {
    std::string account_id = cl->GetSwitchValueASCII(switches::kUserSwitch);
    std::string key_name = cl->GetSwitchValueASCII(switches::kAttrNameSwitch);
    if (key_name.empty()) {
      printf("No key name specified (--%s=<name>)\n",
             switches::kAttrNameSwitch);
      return 1;
    }
    gboolean is_user_specific = !account_id.empty();
    brillo::glib::ScopedError error;
    gboolean success = FALSE;
    if (!org_chromium_CryptohomeInterface_tpm_attestation_delete_key(
            proxy.gproxy(), is_user_specific, account_id.c_str(),
            key_name.c_str(), &success, &brillo::Resetter(&error).lvalue())) {
      printf("AsyncTpmAttestationDeleteKeys call failed: %s.\n",
             error->message);
      return 1;
    }
    if (!success) {
      printf("Delete operation failed.\n");
      return 1;
    }
  } else if (!strcmp(
                 switches::kActions[switches::ACTION_TPM_ATTESTATION_GET_EK],
                 action.c_str())) {
    if (cl->HasSwitch(switches::kProtobufSwitch)) {
      // Get the EK info as a protobuf.
      cryptohome::GetEndorsementInfoRequest request;
      cryptohome::BaseReply reply;
      if (!MakeProtoDBusCall("GetEndorsementInfo",
                             DBUS_METHOD(get_endorsement_info),
                             DBUS_METHOD(get_endorsement_info_async), cl,
                             &proxy, request, &reply, true /* print_reply */)) {
        return 1;
      }
      if (!reply.HasExtension(cryptohome::GetEndorsementInfoReply::reply)) {
        printf("GetEndorsementInfoReply missing.\n");
        return 1;
      }
      printf("GetEndorsementInfo (protobuf) success.\n");
    } else {
      brillo::glib::ScopedError error;
      gboolean success = FALSE;
      gchar* ek_info = NULL;
      if (!org_chromium_CryptohomeInterface_tpm_attestation_get_ek(
              proxy.gproxy(), &ek_info, &success,
              &brillo::Resetter(&error).lvalue())) {
        printf("AsyncTpmAttestationGetEK call failed: %s.\n", error->message);
        return 1;
      }
      if (!success) {
        printf("Failed to get EK.\n");
        g_free(ek_info);
        return 1;
      }
      printf("%s\n", ek_info);
      g_free(ek_info);
    }
  } else if (!strcmp(switches::kActions
                         [switches::ACTION_TPM_ATTESTATION_RESET_IDENTITY],
                     action.c_str())) {
    brillo::glib::ScopedError error;
    gboolean success = FALSE;
    std::string token = cl->GetSwitchValueASCII(switches::kPasswordSwitch);
    brillo::glib::ScopedArray reset_request;
    if (!org_chromium_CryptohomeInterface_tpm_attestation_reset_identity(
            proxy.gproxy(), token.c_str(),
            &brillo::Resetter(&reset_request).lvalue(), &success,
            &brillo::Resetter(&error).lvalue())) {
      printf("TpmAttestationResetIdentity call failed: %s.\n", error->message);
      return 1;
    }
    if (!success) {
      printf("Failed to get identity reset request.\n");
      return 1;
    }
    base::WriteFile(GetOutputFile(cl), reset_request->data, reset_request->len);
  } else if (!strcmp(
                 switches::kActions
                     [switches::ACTION_TPM_ATTESTATION_RESET_IDENTITY_RESULT],
                 action.c_str())) {
    std::string contents;
    if (!base::ReadFileToString(GetInputFile(cl), &contents)) {
      printf("Failed to read input file: %s\n",
             GetInputFile(cl).value().c_str());
      return 1;
    }
    cryptohome::AttestationResetResponse response;
    if (!response.ParseFromString(contents)) {
      printf("Failed to parse response.\n");
      return 1;
    }
    switch (response.status()) {
      case cryptohome::OK:
        printf("Identity reset successful.\n");
        break;
      case cryptohome::SERVER_ERROR:
        printf("Identity reset server error: %s\n", response.detail().c_str());
        break;
      case cryptohome::BAD_REQUEST:
        printf("Identity reset data error: %s\n", response.detail().c_str());
        break;
      case cryptohome::REJECT:
        printf("Identity reset request denied: %s\n",
               response.detail().c_str());
        break;
      case cryptohome::QUOTA_LIMIT_EXCEEDED:
        printf("Identity reset quota exceeded: %s\n",
               response.detail().c_str());
        break;
      default:
        printf("Identity reset unknown error: %s\n", response.detail().c_str());
    }
  } else if (!strcmp(switches::kActions[switches::ACTION_SIGN_LOCKBOX],
                     action.c_str())) {
    std::string data;
    if (!base::ReadFileToString(GetInputFile(cl), &data)) {
      printf("Failed to read input file: %s\n",
             GetInputFile(cl).value().c_str());
      return 1;
    }

    cryptohome::SignBootLockboxRequest request;
    request.set_data(data);
    cryptohome::BaseReply reply;
    if (!MakeProtoDBusCall("SignBootLockbox", DBUS_METHOD(sign_boot_lockbox),
                           DBUS_METHOD(sign_boot_lockbox_async), cl, &proxy,
                           request, &reply, true /* print_reply */)) {
      return 1;
    }

    if (!reply.HasExtension(cryptohome::SignBootLockboxReply::reply)) {
      printf("SignBootLockboxReply missing.\n");
      return 1;
    }
    std::string signature =
        reply.GetExtension(cryptohome::SignBootLockboxReply::reply).signature();
    base::WriteFile(GetOutputFile(cl).AddExtension("signature"),
                    signature.data(), signature.size());
    printf("SignBootLockbox success.\n");
  } else if (!strcmp(switches::kActions[switches::ACTION_VERIFY_LOCKBOX],
                     action.c_str())) {
    std::string data;
    if (!base::ReadFileToString(GetInputFile(cl), &data)) {
      printf("Failed to read input file: %s\n",
             GetInputFile(cl).value().c_str());
      return 1;
    }
    std::string signature;
    FilePath signature_file = GetInputFile(cl).AddExtension("signature");
    if (!base::ReadFileToString(signature_file, &signature)) {
      printf("Failed to read input file: %s\n", signature_file.value().c_str());
      return 1;
    }

    cryptohome::VerifyBootLockboxRequest request;
    request.set_data(data);
    request.set_signature(signature);
    cryptohome::BaseReply reply;
    if (!MakeProtoDBusCall("VerifyBootLockbox",
                           DBUS_METHOD(verify_boot_lockbox),
                           DBUS_METHOD(verify_boot_lockbox_async), cl, &proxy,
                           request, &reply, true /* print_reply */)) {
      return 1;
    }
    printf("VerifyBootLockbox success.\n");
  } else if (!strcmp(switches::kActions[switches::ACTION_FINALIZE_LOCKBOX],
                     action.c_str())) {
    cryptohome::FinalizeBootLockboxRequest request;
    cryptohome::BaseReply reply;
    if (!MakeProtoDBusCall("FinalizeBootLockbox",
                           DBUS_METHOD(finalize_boot_lockbox),
                           DBUS_METHOD(finalize_boot_lockbox_async), cl, &proxy,
                           request, &reply, true /* print_reply */)) {
      return 1;
    }
    printf("FinalizeBootLockbox success.\n");
  } else if (!strcmp(switches::kActions[switches::ACTION_GET_BOOT_ATTRIBUTE],
                     action.c_str())) {
    std::string name;
    if (!GetAttrName(cl, &name)) {
      printf("No attribute name specified.\n");
      return 1;
    }

    cryptohome::GetBootAttributeRequest request;
    request.set_name(name);
    cryptohome::BaseReply reply;
    if (!MakeProtoDBusCall("GetBootAttribute", DBUS_METHOD(get_boot_attribute),
                           DBUS_METHOD(get_boot_attribute_async), cl, &proxy,
                           request, &reply, true /* print_reply */)) {
      return 1;
    }
    if (!reply.HasExtension(cryptohome::GetBootAttributeReply::reply)) {
      printf("GetBootAttributeReply missing.\n");
      return 1;
    }
    std::string value =
        reply.GetExtension(cryptohome::GetBootAttributeReply::reply).value();
    printf("%s\n", value.c_str());
  } else if (!strcmp(switches::kActions[switches::ACTION_SET_BOOT_ATTRIBUTE],
                     action.c_str())) {
    std::string name;
    if (!GetAttrName(cl, &name)) {
      printf("No attribute name specified.\n");
      return 1;
    }
    std::string value;
    if (!GetAttrValue(cl, &value)) {
      printf("No attribute value specified.\n");
      return 1;
    }

    cryptohome::SetBootAttributeRequest request;
    request.set_name(name);
    request.set_value(value);
    cryptohome::BaseReply reply;
    if (!MakeProtoDBusCall("SetBootAttribute", DBUS_METHOD(set_boot_attribute),
                           DBUS_METHOD(set_boot_attribute_async), cl, &proxy,
                           request, &reply, true /* print_reply */)) {
      return 1;
    }
    printf("SetBootAttribute success.\n");
  } else if (!strcmp(switches::kActions
                         [switches::ACTION_FLUSH_AND_SIGN_BOOT_ATTRIBUTES],
                     action.c_str())) {
    cryptohome::FlushAndSignBootAttributesRequest request;
    cryptohome::BaseReply reply;
    if (!MakeProtoDBusCall("FlushAndSignBootAttributes",
                           DBUS_METHOD(flush_and_sign_boot_attributes),
                           DBUS_METHOD(flush_and_sign_boot_attributes_async),
                           cl, &proxy, request, &reply,
                           true /* print_reply */)) {
      return 1;
    }
    printf("FlushAndSignBootAttributes success.\n");
  } else if (!strcmp(switches::kActions[switches::ACTION_GET_LOGIN_STATUS],
                     action.c_str())) {
    cryptohome::GetLoginStatusRequest request;
    cryptohome::BaseReply reply;
    if (!MakeProtoDBusCall("GetLoginStatus", DBUS_METHOD(get_login_status),
                           DBUS_METHOD(get_login_status_async), cl, &proxy,
                           request, &reply, true /* print_reply */)) {
      return 1;
    }
    if (!reply.HasExtension(cryptohome::GetLoginStatusReply::reply)) {
      printf("GetLoginStatusReply missing.\n");
      return 1;
    }
    printf("GetLoginStatus success.\n");
  } else if (!strcmp(switches::kActions[switches::ACTION_INITIALIZE_CAST_KEY],
                     action.c_str())) {
    cryptohome::InitializeCastKeyRequest request;
    cryptohome::BaseReply reply;
    if (!MakeProtoDBusCall("InitializeCastKey",
                           DBUS_METHOD(initialize_cast_key),
                           DBUS_METHOD(initialize_cast_key_async), cl, &proxy,
                           request, &reply, true /* print_reply */)) {
      return 1;
    }
    printf("InitializeCastKey success.\n");
  } else if (!strcmp(switches::kActions
                         [switches::ACTION_GET_FIRMWARE_MANAGEMENT_PARAMETERS],
                     action.c_str())) {
    cryptohome::GetFirmwareManagementParametersRequest request;
    cryptohome::BaseReply reply;
    if (!MakeProtoDBusCall(
            cryptohome::kCryptohomeGetFirmwareManagementParameters,
            DBUS_METHOD(get_firmware_management_parameters),
            DBUS_METHOD(get_firmware_management_parameters_async), cl, &proxy,
            request, &reply, true /* print_reply */)) {
      return 1;
    }
    if (!reply.HasExtension(
            cryptohome::GetFirmwareManagementParametersReply::reply)) {
      printf("GetFirmwareManagementParametersReply missing.\n");
      return 1;
    }
    cryptohome::GetFirmwareManagementParametersReply get_fwmp_reply =
        reply.GetExtension(
            cryptohome::GetFirmwareManagementParametersReply::reply);
    printf("flags=0x%08x\n", get_fwmp_reply.flags());
    brillo::Blob hash =
        brillo::BlobFromString(get_fwmp_reply.developer_key_hash());
    printf("hash=%s\n", cryptohome::CryptoLib::BlobToHex(hash).c_str());
    printf("GetFirmwareManagementParameters success.\n");
  } else if (!strcmp(switches::kActions
                         [switches::ACTION_SET_FIRMWARE_MANAGEMENT_PARAMETERS],
                     action.c_str())) {
    cryptohome::SetFirmwareManagementParametersRequest request;
    cryptohome::BaseReply reply;

    if (cl->HasSwitch(switches::kFlagsSwitch)) {
      std::string flags_str = cl->GetSwitchValueASCII(switches::kFlagsSwitch);
      char* end = NULL;
      int32_t flags = strtol(flags_str.c_str(), &end, 0);
      if (end && *end != '\0') {
        printf("Bad flags value.\n");
        return 1;
      }
      request.set_flags(flags);
    } else {
      printf("Use --flags (and optionally --developer_key_hash).\n");
      return 1;
    }

    if (cl->HasSwitch(switches::kDevKeyHashSwitch)) {
      std::string hash_str =
          cl->GetSwitchValueASCII(switches::kDevKeyHashSwitch);
      brillo::Blob hash;
      if (!base::HexStringToBytes(hash_str, &hash)) {
        printf("Bad hash value.\n");
        return 1;
      }
      if (hash.size() != SHA256_DIGEST_LENGTH) {
        printf("Bad hash size.\n");
        return 1;
      }

      request.set_developer_key_hash(brillo::BlobToString(hash));
    }

    if (!MakeProtoDBusCall(
            cryptohome::kCryptohomeSetFirmwareManagementParameters,
            DBUS_METHOD(set_firmware_management_parameters),
            DBUS_METHOD(set_firmware_management_parameters_async), cl, &proxy,
            request, &reply, true /* print_reply */)) {
      return 1;
    }
    printf("SetFirmwareManagementParameters success.\n");
  } else if (!strcmp(
                 switches::kActions
                     [switches::ACTION_REMOVE_FIRMWARE_MANAGEMENT_PARAMETERS],
                 action.c_str())) {
    cryptohome::RemoveFirmwareManagementParametersRequest request;
    cryptohome::BaseReply reply;

    if (!MakeProtoDBusCall(
            cryptohome::kCryptohomeRemoveFirmwareManagementParameters,
            DBUS_METHOD(remove_firmware_management_parameters),
            DBUS_METHOD(remove_firmware_management_parameters_async), cl,
            &proxy, request, &reply, true /* print_reply */)) {
      return 1;
    }
    printf("RemoveFirmwareManagementParameters success.\n");
  } else if (!strcmp(switches::kActions[switches::ACTION_MIGRATE_TO_DIRCRYPTO],
                     action.c_str())) {
    cryptohome::AccountIdentifier id;
    if (!BuildAccountId(cl, &id))
      return 1;

    brillo::glib::ScopedArray account_ary(GArrayFromProtoBuf(id));
    if (!account_ary.get())
      return 1;

    cryptohome::MigrateToDircryptoRequest request;
    request.set_minimal_migration(cl->HasSwitch(switches::kMinimalMigration));

    brillo::glib::ScopedArray request_ary(GArrayFromProtoBuf(request));
    if (!request_ary.get())
      return 1;

    brillo::glib::ScopedError error;
    if (!org_chromium_CryptohomeInterface_migrate_to_dircrypto(
            proxy.gproxy(), account_ary.get(), request_ary.get(),
            &brillo::Resetter(&error).lvalue())) {
      printf("MigrateToDircrypto call failed: %s\n", error->message);
      return 1;
    }
    printf("MigrateToDircrypto call succeeded.\n");
  } else if (!strcmp(
                 switches::kActions[switches::ACTION_NEEDS_DIRCRYPTO_MIGRATION],
                 action.c_str())) {
    cryptohome::AccountIdentifier id;
    if (!BuildAccountId(cl, &id)) {
      printf("No account_id specified.\n");
      return 1;
    }

    brillo::glib::ScopedArray account_ary(GArrayFromProtoBuf(id));
    if (!account_ary.get())
      return 1;

    brillo::glib::ScopedError error;
    gboolean needs_migration = false;
    if (!org_chromium_CryptohomeInterface_needs_dircrypto_migration(
            proxy.gproxy(), account_ary.get(), &needs_migration,
            &brillo::Resetter(&error).lvalue())) {
      printf("NeedsDirCryptoMigration call failed: %s.\n", error->message);
      return 1;
    }
    if (needs_migration)
      printf("Yes\n");
    else
      printf("No\n");
  } else if (!strcmp(switches::kActions[switches::ACTION_GET_ENROLLMENT_ID],
                     action.c_str())) {
    gboolean success = FALSE;
    brillo::glib::ScopedArray enrollment_id;
    brillo::glib::ScopedError error;
    if (!org_chromium_CryptohomeInterface_tpm_attestation_get_enrollment_id(
            proxy.gproxy(), cl->HasSwitch(switches::kIgnoreCache),
            &brillo::Resetter(&enrollment_id).lvalue(), &success,
            &brillo::Resetter(&error).lvalue())) {
      printf("GetEnrollmentId call failed: %s.\n", error->message);
      return 1;
    }
    std::string eid_str = base::ToLowerASCII(
        base::HexEncode(enrollment_id->data, enrollment_id->len));
    printf("%s\n", eid_str.c_str());
  } else if (!strcmp(switches::kActions
                         [switches::ACTION_GET_SUPPORTED_KEY_POLICIES],
                     action.c_str())) {
    cryptohome::GetSupportedKeyPoliciesRequest request;
    cryptohome::BaseReply reply;

    if (!MakeProtoDBusCall(cryptohome::kCryptohomeGetSupportedKeyPolicies,
                           DBUS_METHOD(get_supported_key_policies),
                           DBUS_METHOD(get_supported_key_policies_async), cl,
                           &proxy, request, &reply, true /* print_reply */)) {
      return 1;
    }
    if (!reply.HasExtension(cryptohome::GetSupportedKeyPoliciesReply::reply)) {
      printf("GetSupportedKeyPoliciesReply missing.\n");
      return 1;
    }
    printf("GetSupportedKeyPolicies success.\n");
  } else if (!strcmp(
                 switches::kActions[switches::ACTION_GET_ACCOUNT_DISK_USAGE],
                 action.c_str())) {
    cryptohome::AccountIdentifier id;
    if (!BuildAccountId(cl, &id))
      return 1;

    brillo::glib::ScopedArray id_ary(GArrayFromProtoBuf(id));
    brillo::glib::ScopedError error;
    GArray* out_reply = NULL;
    if (!org_chromium_CryptohomeInterface_get_account_disk_usage(
            proxy.gproxy(), id_ary.get(), &out_reply,
            &brillo::Resetter(&error).lvalue())) {
      printf("GetAccountDiskUsage call failed: %s", error->message);
      return 1;
    }
    cryptohome::BaseReply reply;
    ParseBaseReply(out_reply, &reply, true /* print_reply */);
    if (reply.has_error()) {
      printf("GetAccountDiskUsage failed with %d.\n", reply.error());
      return reply.error();
    }
    if (!reply.HasExtension(cryptohome::GetAccountDiskUsageReply::reply)) {
      printf("GetAccountDiskUsageReply missing.\n");
      return 1;
    }
    auto ext = reply.GetExtension(cryptohome::GetAccountDiskUsageReply::reply);
    printf("Account Disk Usage in bytes: %" PRId64 "\n", ext.size());
    return 0;
  } else if (!strcmp(
                 switches::kActions
                     [switches::ACTION_LOCK_TO_SINGLE_USER_MOUNT_UNTIL_REBOOT],
                 action.c_str())) {
    cryptohome::AccountIdentifier id;
    if (!BuildAccountId(cl, &id))
      return 1;

    cryptohome::LockToSingleUserMountUntilRebootRequest request;
    request.mutable_account_id()->CopyFrom(id);
    brillo::glib::ScopedArray req_ary(GArrayFromProtoBuf(request));
    cryptohome::BaseReply reply;
    brillo::glib::ScopedError error;
    GArray* out_reply = NULL;
    if (!org_chromium_CryptohomeInterface_lock_to_single_user_mount_until_reboot(
            proxy.gproxy(), req_ary.get(), &out_reply,
            &brillo::Resetter(&error).lvalue())) {
      printf("LockToSingleUserMountUntilReboot call failed: %s",
             error->message);
      return 1;
    }
    ParseBaseReply(out_reply, &reply, true /* print_reply */);
    printf("Login disabled.\n");
  } else if (!strcmp(switches::kActions[switches::ACTION_GET_RSU_DEVICE_ID],
                     action.c_str())) {
    cryptohome::GetRsuDeviceIdRequest request;
    cryptohome::BaseReply reply;
    if (!MakeProtoDBusCall("GetRsuDeviceId", DBUS_METHOD(get_rsu_device_id),
                           DBUS_METHOD(get_rsu_device_id_async), cl, &proxy,
                           request, &reply, true /* print_reply */)) {
      return 1;
    }
    if (!reply.HasExtension(cryptohome::GetRsuDeviceIdReply::reply)) {
      printf("GetRsuDeviceIdReply missing.\n");
      return 1;
    }
  } else if (!strcmp(switches::kActions[switches::ACTION_CHECK_HEALTH],
                     action.c_str())) {
    cryptohome::CheckHealthRequest request;
    cryptohome::BaseReply reply;
    if (!MakeProtoDBusCall("CheckHealth", DBUS_METHOD(check_health),
                           DBUS_METHOD(check_health_async), cl, &proxy, request,
                           &reply, true /* print_reply */)) {
      return 1;
    }
    if (!reply.HasExtension(cryptohome::CheckHealthReply::reply)) {
      printf("CheckHealthReply missing.\n");
      return 1;
    }
  } else {
    printf("Unknown action or no action given.  Available actions:\n");
    for (int i = 0; switches::kActions[i]; i++)
      printf("  --action=%s\n", switches::kActions[i]);
  }
  return 0;
}  // NOLINT(readability/fn_size)
