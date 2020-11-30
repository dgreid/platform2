// Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/command_line.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/stringprintf.h>
#include <brillo/process/process_mock.h>
#include <brillo/syslog_logging.h>
#include <brillo/test_helpers.h>
#include <gtest/gtest.h>

#include "vpn-manager/daemon_mock.h"
#include "vpn-manager/ipsec_manager.h"

using ::base::FilePath;
using ::brillo::FindLog;
using ::brillo::ProcessMock;
using ::testing::_;
using ::testing::InSequence;
using ::testing::Return;

namespace vpn_manager {

namespace {

const int kMockFd = 123;

const int kMockStarterPid = 10001;

}  // namespace

class IpsecManagerTest : public ::testing::Test {
 public:
  IpsecManagerTest() : starter_daemon_(nullptr), charon_daemon_(nullptr) {}
  ~IpsecManagerTest() override = default;

  static FilePath GetSourceDirectory() {
    FilePath src_dir;
    const char* src_env = getenv("SRC");
    if (src_env) {
      src_dir = FilePath(src_env);
    }
    return src_dir;
  }

  void SetUp() override {
    CHECK(temp_dir_.CreateUniqueTempDir());
    test_path_ = temp_dir_.GetPath().Append("ipsec_manager_testdir");
    base::DeletePathRecursively(test_path_);
    base::CreateDirectory(test_path_);
    persistent_path_ = test_path_.Append("persistent");
    base::CreateDirectory(persistent_path_);
    remote_address_text_ = "1.2.3.4";
    ASSERT_TRUE(ServiceManager::ConvertIPStringToSockAddr(remote_address_text_,
                                                          &remote_address_));
    psk_file_ = test_path_.Append("psk").value();
    xauth_credentials_file_ = test_path_.Append("xauth_credentials").value();
    server_ca_file_ = test_path_.Append("server.ca").value();
    WriteFile(server_ca_file_, "contents not used for testing");
    server_id_ = "CN=vpnserver";
    client_cert_tpm_slot_ = "0";
    client_cert_tpm_id_ = "0a";
    tpm_user_pin_ = "123456";
    ipsec_run_path_ = test_path_.Append("run").value();
    // Ipsec runtime data dir is created in shill init script, so just create
    // it here as well for the purposes of this test.
    base::CreateDirectory(FilePath(ipsec_run_path_));
    ipsec_up_file_ = FilePath(ipsec_run_path_).Append("up").value();
    WriteFile(psk_file_, "secret");
    WriteFile(
        xauth_credentials_file_,
        base::StringPrintf("%s\n%s\n", kXauthUser, kXauthPassword).c_str());
    base::CopyFile(GetSourceDirectory().Append("testdata/cacert.der"),
                   FilePath(server_ca_file_));
    brillo::ClearLog();
    starter_daemon_ = new DaemonMock;
    charon_daemon_ = new DaemonMock;
    ipsec_.reset(
        new IpsecManager("aes128-sha1,3des-sha1,aes128-md5,3des-md5",  // esp
                         "3des-sha1-modp1024",                         // ike
                         30,                         // ipsec_timeout
                         "17/1701",                  // left_protoport
                         true,                       // rekey
                         "17/1701",                  // right_protoport
                         "",                         // tunnel_group
                         "transport",                // type
                         test_path_,                 // temp_path
                         persistent_path_));         // persistent_path
    ipsec_->starter_daemon_.reset(starter_daemon_);  // Passes ownership.
    ipsec_->charon_daemon_.reset(charon_daemon_);    // Passes ownership.
    ipsec_->ipsec_group_ = getgid();
    ipsec_->ipsec_up_file_ = ipsec_up_file_;
    ipsec_->force_local_address_ = "5.6.7.8";
  }

  // Creates a ProcessMock that will be handed to to the IpsecManager when
  // it creates a process.  The caller owns this mock and is responsible for
  // disposing of it (since the IpsecManager assumes the Daemon owns it).
  ProcessMock* SetStartStarterExpectations();

 protected:
  static const char kXauthUser[];
  static const char kXauthPassword[];

  void WriteFile(const std::string& file_path, const char* contents) {
    if (base::WriteFile(FilePath(file_path), contents, strlen(contents)) < 0) {
      LOG(ERROR) << "Unable to create " << file_path;
    }
  }

  void DoInitialize(int ike_version, bool use_psk, bool use_xauth);

  std::unique_ptr<IpsecManager> ipsec_;
  FilePath persistent_path_;
  FilePath test_path_;
  std::string remote_address_text_;
  sockaddr_storage remote_address_;
  std::string psk_file_;
  std::string server_ca_file_;
  std::string xauth_credentials_file_;
  std::string server_id_;
  std::string client_cert_tpm_slot_;
  std::string client_cert_tpm_id_;
  std::string tpm_user_pin_;
  std::string ipsec_run_path_;
  std::string ipsec_up_file_;
  // These mock daemons are owned by the ipsec instance and tracked here only
  // for expectations.
  DaemonMock* starter_daemon_;
  DaemonMock* charon_daemon_;
  base::ScopedTempDir temp_dir_;
};

// static
const char IpsecManagerTest::kXauthUser[] = "xauth_user";
const char IpsecManagerTest::kXauthPassword[] = "xauth_password";

void IpsecManagerTest::DoInitialize(int ike_version,
                                    bool use_psk,
                                    bool use_xauth) {
  std::string xauth_file;
  if (use_xauth) {
    xauth_file = xauth_credentials_file_;
  }
  if (use_psk) {
    ASSERT_TRUE(ipsec_->Initialize(ike_version, remote_address_, psk_file_,
                                   xauth_file, "", "", "", "", ""));
  } else {
    ASSERT_TRUE(ipsec_->Initialize(
        ike_version, remote_address_, "", xauth_file, server_ca_file_,
        server_id_, client_cert_tpm_slot_, client_cert_tpm_id_, tpm_user_pin_));
  }
}

ProcessMock* IpsecManagerTest::SetStartStarterExpectations() {
  EXPECT_CALL(*starter_daemon_, FindProcess());
  EXPECT_CALL(*charon_daemon_, FindProcess());
  EXPECT_CALL(*starter_daemon_, ClearProcess());
  EXPECT_CALL(*charon_daemon_, ClearProcess());
  ProcessMock* process = new ProcessMock;
  // Does not pass ownership.
  EXPECT_CALL(*starter_daemon_, CreateProcessWithResourceLimits(_))
      .WillOnce(Return(process));
  EXPECT_CALL(*process, AddArg(IPSEC_STARTER));
  EXPECT_CALL(*process, AddArg("--nofork"));
  EXPECT_CALL(*process, RedirectUsingPipe(STDERR_FILENO, false));
  EXPECT_CALL(*process, Start()).WillOnce(Return(true));
  EXPECT_CALL(*process, GetPipe(STDERR_FILENO)).WillOnce(Return(kMockFd));
  EXPECT_CALL(*process, pid()).WillOnce(Return(kMockStarterPid));
  return process;
}

TEST_F(IpsecManagerTest, InitializeNoAuth) {
  EXPECT_FALSE(
      ipsec_->Initialize(1, remote_address_, "", "", "", "", "", "", ""));
  EXPECT_TRUE(FindLog("Must specify either PSK or certificates"));
}

TEST_F(IpsecManagerTest, InitializeNotBoth) {
  EXPECT_TRUE(ipsec_->Initialize(
      1, remote_address_, psk_file_, "", server_ca_file_, server_id_,
      client_cert_tpm_slot_, client_cert_tpm_id_, tpm_user_pin_));
  EXPECT_TRUE(FindLog("Specified both certificates and PSK to IPsec layer"));
}

TEST_F(IpsecManagerTest, InitializeUnsupportedVersion) {
  EXPECT_FALSE(ipsec_->Initialize(3, remote_address_, psk_file_, "", "", "", "",
                                  "", ""));
  EXPECT_TRUE(FindLog("Unsupported IKE version"));
}

TEST_F(IpsecManagerTest, InitializeIkev2WithCertificates) {
  EXPECT_FALSE(ipsec_->Initialize(2, remote_address_, "", "", server_ca_file_,
                                  server_id_, client_cert_tpm_slot_,
                                  client_cert_tpm_id_, tpm_user_pin_));
  EXPECT_TRUE(FindLog("Only IKE version 1 is supported with certificates"));
}

TEST_F(IpsecManagerTest, PollWaitIfNotUpYet) {
  ipsec_->start_ticks_ = base::TimeTicks::Now();
  EXPECT_EQ(1000, ipsec_->Poll());
}

TEST_F(IpsecManagerTest, PollTimeoutWaiting) {
  int ipsec_timeout = 30;
  if (base::CommandLine::InitializedForCurrentProcess()) {
    base::CommandLine* cl = base::CommandLine::ForCurrentProcess();
    if (cl->HasSwitch("ipsec_timeout")) {
      int conversion = 0;
      if (base::StringToInt(cl->GetSwitchValueASCII("ipsec_timeout"),
                            &conversion))
        ipsec_timeout = conversion;
    }
  }

  ipsec_->start_ticks_ =
      base::TimeTicks::Now() - base::TimeDelta::FromSeconds(ipsec_timeout + 1);
  EXPECT_EQ(1000, ipsec_->Poll());
  EXPECT_TRUE(FindLog("IPsec connection timed out"));
  EXPECT_TRUE(ipsec_->was_stopped());
}

TEST_F(IpsecManagerTest, PollTransitionToUp) {
  ipsec_->start_ticks_ = base::TimeTicks::Now();
  EXPECT_TRUE(base::PathExists(FilePath(ipsec_run_path_)));
  WriteFile(ipsec_up_file_, "");
  EXPECT_FALSE(ipsec_->is_running());
  EXPECT_EQ(-1, ipsec_->Poll());
  EXPECT_TRUE(FindLog("IPsec connection now up"));
  EXPECT_TRUE(ipsec_->is_running());
}

TEST_F(IpsecManagerTest, PollNothingIfRunning) {
  ipsec_->is_running_ = true;
  EXPECT_EQ(-1, ipsec_->Poll());
}

TEST_F(IpsecManagerTest, FormatSecretsNoSlot) {
  client_cert_tpm_slot_ = "";
  DoInitialize(1, false, false);
  std::string formatted;
  EXPECT_TRUE(ipsec_->FormatSecrets(&formatted));
  EXPECT_EQ("5.6.7.8 1.2.3.4 : PIN %smartcard0@crypto_module:0a \"123456\"\n",
            formatted);
}

TEST_F(IpsecManagerTest, FormatSecretsNonZeroSlot) {
  client_cert_tpm_slot_ = "1";
  DoInitialize(1, false, false);
  std::string formatted;
  EXPECT_TRUE(ipsec_->FormatSecrets(&formatted));
  EXPECT_EQ("5.6.7.8 1.2.3.4 : PIN %smartcard1@crypto_module:0a \"123456\"\n",
            formatted);
}

TEST_F(IpsecManagerTest, FormatSecretsXauthCredentials) {
  client_cert_tpm_slot_ = "1";
  DoInitialize(1, false, true);
  std::string formatted;
  EXPECT_TRUE(ipsec_->FormatSecrets(&formatted));
  EXPECT_EQ(
      base::StringPrintf(
          "5.6.7.8 1.2.3.4 : PIN %%smartcard1@crypto_module:0a \"123456\"\n"
          "%s : XAUTH \"%s\"\n",
          kXauthUser, kXauthPassword),
      formatted);
  EXPECT_EQ(kXauthUser, ipsec_->xauth_identity_);
}

TEST_F(IpsecManagerTest, FormatStrongswanConfigFile) {
  std::string strongswan_config(
      "libstrongswan {\n"
      "  plugins {\n"
      "    pkcs11 {\n"
      "      modules {\n"
      "        crypto_module {\n"
      "          path = " PKCS11_LIB
      "\n"
      "        }\n"
      "      }\n"
      "    }\n"
      "  }\n"
      "}\n"
      "charon {\n"
      "  accept_unencrypted_mainmode_messages = yes\n"
      "  ignore_routing_tables = 0\n"
      "  install_routes = no\n"
      "  routing_table = 0\n"
      "}\n");
  EXPECT_EQ(strongswan_config, ipsec_->FormatStrongswanConfigFile());
}

TEST_F(IpsecManagerTest, StartStarter) {
  InSequence unused;
  std::unique_ptr<ProcessMock> process(SetStartStarterExpectations());
  EXPECT_TRUE(ipsec_->StartStarter());
  EXPECT_EQ(kMockFd, ipsec_->output_fd());
  EXPECT_EQ("ipsec[10001]: ", ipsec_->ipsec_prefix_);
}

TEST_F(IpsecManagerTest, StopWhileRunning) {
  InSequence unused;
  EXPECT_CALL(*starter_daemon_, IsRunning()).WillOnce(Return(true));
  EXPECT_CALL(*starter_daemon_, FindProcess()).Times(0);
  EXPECT_CALL(*charon_daemon_, FindProcess()).Times(1);
  EXPECT_CALL(*starter_daemon_, Terminate()).WillOnce(Return(true));
  EXPECT_CALL(*charon_daemon_, Terminate()).WillOnce(Return(false));
  ipsec_->Stop();
}

TEST_F(IpsecManagerTest, StopWhileNotRunning) {
  InSequence unused;
  EXPECT_CALL(*starter_daemon_, IsRunning()).WillOnce(Return(false));
  EXPECT_CALL(*starter_daemon_, FindProcess()).Times(1);
  EXPECT_CALL(*charon_daemon_, FindProcess()).Times(1);
  EXPECT_CALL(*starter_daemon_, Terminate()).WillOnce(Return(false));
  EXPECT_CALL(*charon_daemon_, Terminate()).WillOnce(Return(false));
  ipsec_->Stop();
}

class IpsecManagerTestIkeV1Psk : public IpsecManagerTest {
 public:
  void SetUp() override {
    IpsecManagerTest::SetUp();
    DoInitialize(1, true, false);
  }

 protected:
  std::string GetExpectedStarter(bool debug, bool xauth);
};

TEST_F(IpsecManagerTestIkeV1Psk, Initialize) {}

TEST_F(IpsecManagerTestIkeV1Psk, FormatSecrets) {
  FilePath input(psk_file_);
  const char psk[] = "pAssword\n";
  base::WriteFile(input, psk, strlen(psk));
  std::string formatted;
  EXPECT_TRUE(ipsec_->FormatSecrets(&formatted));
  EXPECT_EQ("5.6.7.8 1.2.3.4 : PSK \"pAssword\"\n", formatted);
}

std::string IpsecManagerTestIkeV1Psk::GetExpectedStarter(bool debug,
                                                         bool xauth) {
  std::string expected("config setup\n");
  if (debug) {
    expected.append("\tcharondebug=\"ike 4, cfg 2, knl 4, net 3, enc 1\"\n");
  } else {
    expected.append("\tcharondebug=\"ike 2, cfg 2, knl 2, net 1, enc 1\"\n");
  }

  expected.append("\tuniqueids=\"no\"\n");
  expected.append(
      "conn managed\n"
      "\tike=\"3des-sha1-modp1024\"\n"
      "\tesp=\"aes128-sha1,3des-sha1,aes128-md5,3des-md5\"\n"
      "\tkeyexchange=\"ikev1\"\n");

  if (xauth) {
    expected.append(
        base::StringPrintf("\tauthby=\"xauthpsk\"\n"
                           "\txauth=\"client\"\n"
                           "\txauth_identity=\"%s\"\n",
                           kXauthUser));
  } else {
    expected.append("\tauthby=\"psk\"\n");
  }

  expected.append(
      "\trekey=yes\n"
      "\tleft=\"%defaultroute\"\n"
      "\tleftprotoport=\"17/1701\"\n"
      "\tleftupdown=\"/usr/libexec/l2tpipsec_vpn/pluto_updown\"\n"
      "\tright=\"1.2.3.4\"\n"
      "\trightid=\"%any\"\n"
      "\trightprotoport=\"17/1701\"\n"
      "\ttype=\"transport\"\n"
      "\tauto=\"start\"\n");
  return expected;
}

TEST_F(IpsecManagerTestIkeV1Psk, FormatStarterConfigFile) {
  EXPECT_EQ(GetExpectedStarter(false, false),
            ipsec_->FormatStarterConfigFile());
  logging::SetMinLogLevel(-5);
  EXPECT_EQ(GetExpectedStarter(true, false), ipsec_->FormatStarterConfigFile());
  ipsec_->xauth_identity_ = kXauthUser;
  EXPECT_EQ(GetExpectedStarter(true, true), ipsec_->FormatStarterConfigFile());
  logging::SetMinLogLevel(0);
  EXPECT_EQ(GetExpectedStarter(false, true), ipsec_->FormatStarterConfigFile());
}

TEST_F(IpsecManagerTestIkeV1Psk, Start) {
  InSequence unused;
  std::unique_ptr<ProcessMock> process(SetStartStarterExpectations());
  EXPECT_TRUE(ipsec_->Start());
  EXPECT_FALSE(ipsec_->start_ticks_.is_null());
}

TEST_F(IpsecManagerTestIkeV1Psk, WriteConfigFiles) {
  logging::SetMinLogLevel(0);
  EXPECT_TRUE(ipsec_->WriteConfigFiles());
  std::string conf_contents;
  ASSERT_TRUE(base::ReadFileToString(persistent_path_.Append("ipsec.conf"),
                                     &conf_contents));
  EXPECT_EQ(GetExpectedStarter(false, false), conf_contents);
  ASSERT_TRUE(base::PathExists(persistent_path_.Append("ipsec.secrets")));
}

class IpsecManagerTestIkeV1Certs : public IpsecManagerTest {
 public:
  void SetUp() override {
    IpsecManagerTest::SetUp();
    DoInitialize(1, false, false);
  }

 protected:
  std::string GetExpectedStarter(bool debug);
};

TEST_F(IpsecManagerTestIkeV1Certs, Initialize) {
  DoInitialize(1, false, false);
}

TEST_F(IpsecManagerTestIkeV1Certs, FormatSecrets) {
  std::string formatted;
  EXPECT_TRUE(ipsec_->FormatSecrets(&formatted));
  EXPECT_EQ("5.6.7.8 1.2.3.4 : PIN %smartcard0@crypto_module:0a \"123456\"\n",
            formatted);
}

std::string IpsecManagerTestIkeV1Certs::GetExpectedStarter(bool debug) {
  std::string expected("config setup\n");
  if (debug) {
    expected.append("\tcharondebug=\"ike 4, cfg 2, knl 4, net 3, enc 1\"\n");
  } else {
    expected.append("\tcharondebug=\"ike 2, cfg 2, knl 2, net 1, enc 1\"\n");
  }

  expected.append("\tuniqueids=\"no\"\n");
  expected.append(
      "conn managed\n"
      "\tike=\"3des-sha1-modp1024\"\n"
      "\tesp=\"aes128-sha1,3des-sha1,aes128-md5,3des-md5\"\n"
      "\tkeyexchange=\"ikev1\"\n"
      "\trekey=yes\n"
      "\tleft=\"%defaultroute\"\n"
      "\tleftcert=\"%smartcard0@crypto_module:0a\"\n"
      "\tleftprotoport=\"17/1701\"\n"
      "\tleftupdown=\"/usr/libexec/l2tpipsec_vpn/pluto_updown\"\n"
      "\tright=\"1.2.3.4\"\n"
      "\trightca=\"C=US, O=simonjam, CN=rootca\"\n"
      "\trightid=\"CN=vpnserver\"\n"
      "\trightprotoport=\"17/1701\"\n"
      "\ttype=\"transport\"\n"
      "\tauto=\"start\"\n");
  return expected;
}

TEST_F(IpsecManagerTestIkeV1Certs, FormatStarterConfigFile) {
  logging::SetMinLogLevel(0);
  EXPECT_EQ(GetExpectedStarter(false), ipsec_->FormatStarterConfigFile());
  logging::SetMinLogLevel(-5);
  EXPECT_EQ(GetExpectedStarter(true), ipsec_->FormatStarterConfigFile());

  // The xauth parameters aren't pertinent to certificate-based authentication.
  ipsec_->xauth_identity_ = kXauthUser;
  EXPECT_EQ(GetExpectedStarter(true), ipsec_->FormatStarterConfigFile());
}

TEST_F(IpsecManagerTestIkeV1Certs, WriteConfigFiles) {
  logging::SetMinLogLevel(0);
  EXPECT_TRUE(ipsec_->WriteConfigFiles());
  std::string conf_contents;
  ASSERT_TRUE(base::ReadFileToString(persistent_path_.Append("ipsec.conf"),
                                     &conf_contents));
  EXPECT_EQ(GetExpectedStarter(false), conf_contents);
  ASSERT_TRUE(base::PathExists(persistent_path_.Append("ipsec.secrets")));
  ASSERT_TRUE(base::PathExists(persistent_path_.Append("cacert.der")));
}

class IpsecManagerTestBogusCerts : public IpsecManagerTest {
 public:
  void SetUp() override {
    IpsecManagerTest::SetUp();
    // testdata/bogus_cacert.der is generated as follows:
    //
    //   openssl genrsa -des3 -out ca.key 512
    //   openssl req -new -x509 -days 365 -key ca.key -out bogus_cacert.pem \
    //       -subj "/CN=$(printf 'a \r b \n c \t d \f e \b f " g \\\\')"
    //   openssl x509 -in bogus_cacert.pem -out bogus_cacert.der -outform DER
    //
    //  The subject field contains a CN attribute with special characters that
    //  require escaping.
    FilePath bogus_server_ca_file = test_path_.Append("bogus_server.ca");
    base::CopyFile(GetSourceDirectory().Append("testdata/bogus_cacert.der"),
                   bogus_server_ca_file);
    ASSERT_TRUE(ipsec_->Initialize(
        1, remote_address_, "", "", bogus_server_ca_file.value(), server_id_,
        client_cert_tpm_slot_, client_cert_tpm_id_, tpm_user_pin_));
  }
};

TEST_F(IpsecManagerTestBogusCerts, FormatStarterConfigFile) {
  std::string expected =
      "config setup\n"
      "\tcharondebug=\"ike 2, cfg 2, knl 2, net 1, enc 1\"\n"
      "\tuniqueids=\"no\"\n"
      "conn managed\n"
      "\tike=\"3des-sha1-modp1024\"\n"
      "\tesp=\"aes128-sha1,3des-sha1,aes128-md5,3des-md5\"\n"
      "\tkeyexchange=\"ikev1\"\n"
      "\trekey=yes\n"
      "\tleft=\"%defaultroute\"\n"
      "\tleftcert=\"%smartcard0@crypto_module:0a\"\n"
      "\tleftprotoport=\"17/1701\"\n"
      "\tleftupdown=\"/usr/libexec/l2tpipsec_vpn/pluto_updown\"\n"
      "\tright=\"1.2.3.4\"\n"
      // The value of the rightca option should contain an escaped version of
      // the subject field in the testdata/bogus_cacert.der file.
      "\trightca=\"CN=a \\r b \\n c \\t d \\f e \\b f \\\" g \\\\\"\n"
      "\trightid=\"CN=vpnserver\"\n"
      "\trightprotoport=\"17/1701\"\n"
      "\ttype=\"transport\"\n"
      "\tauto=\"start\"\n";
  logging::SetMinLogLevel(0);
  EXPECT_EQ(expected, ipsec_->FormatStarterConfigFile());
}

}  // namespace vpn_manager
