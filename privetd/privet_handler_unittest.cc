// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "privetd/privet_handler.h"

#include <set>
#include <string>
#include <utility>

#include <base/bind.h>
#include <base/json/json_reader.h>
#include <base/json/json_writer.h>
#include <base/run_loop.h>
#include <base/strings/string_util.h>
#include <base/values.h>
#include <chromeos/http/http_request.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "privetd/constants.h"
#include "privetd/mock_delegates.h"

using testing::_;
using testing::DoAll;
using testing::Invoke;
using testing::Return;
using testing::SetArgPointee;

namespace privetd {

namespace {

void LoadTestJson(const std::string& test_json,
                  base::DictionaryValue* dictionary) {
  std::string json = test_json;
  base::ReplaceChars(json, "'", "\"", &json);
  int error = 0;
  std::string message;
  std::unique_ptr<base::Value> value(base::JSONReader::ReadAndReturnError(
      json, base::JSON_PARSE_RFC, &error, &message));
  EXPECT_TRUE(value.get()) << "\nError: " << message << "\n" << json;
  base::DictionaryValue* dictionary_ptr = nullptr;
  if (value->GetAsDictionary(&dictionary_ptr))
    dictionary->MergeDictionary(dictionary_ptr);
}

bool IsEqualValue(const base::Value& val1, const base::Value& val2) {
  return val1.Equals(&val2);
}

struct CodeWithReason {
  CodeWithReason(int code_in, const std::string& reason_in)
      : code(code_in), reason(reason_in) {}
  int code;
  std::string reason;
};

std::ostream& operator<<(std::ostream& stream, const CodeWithReason& error) {
  return stream << "{" << error.code << ", " << error.reason << "}";
}

bool IsEqualError(const CodeWithReason& expected,
                  const base::DictionaryValue& dictionary) {
  std::string reason;
  int code = 0;
  return dictionary.GetInteger("error.http_status", &code) &&
         code == expected.code && dictionary.GetString("error.code", &reason) &&
         reason == expected.reason;
}

bool IsEqualDictionary(const base::DictionaryValue& dictionary1,
                       const base::DictionaryValue& dictionary2) {
  base::DictionaryValue::Iterator it1(dictionary1);
  base::DictionaryValue::Iterator it2(dictionary2);
  for (; !it1.IsAtEnd() && !it2.IsAtEnd(); it1.Advance(), it2.Advance()) {
    // Output mismatched keys.
    EXPECT_EQ(it1.key(), it2.key());
    if (it1.key() != it2.key())
      return false;

    if (it1.key() == "error") {
      std::string code1;
      std::string code2;
      const char kCodeKey[] = "error.code";
      if (!dictionary1.GetString(kCodeKey, &code1) ||
          !dictionary2.GetString(kCodeKey, &code2) || code1 != code2) {
        return false;
      }
      continue;
    }

    const base::DictionaryValue* d1{nullptr};
    const base::DictionaryValue* d2{nullptr};
    if (it1.value().GetAsDictionary(&d1) && it2.value().GetAsDictionary(&d2)) {
      if (!IsEqualDictionary(*d1, *d2))
        return false;
      continue;
    }

    // Output mismatched values.
    EXPECT_PRED2(IsEqualValue, it1.value(), it2.value());
    if (!IsEqualValue(it1.value(), it2.value()))
      return false;
  }

  return it1.IsAtEnd() && it2.IsAtEnd();
}

bool IsEqualJson(const std::string& test_json,
                 const base::DictionaryValue& dictionary) {
  base::DictionaryValue dictionary2;
  LoadTestJson(test_json, &dictionary2);
  return IsEqualDictionary(dictionary2, dictionary);
}

}  // namespace

class PrivetHandlerTest : public testing::Test {
 public:
  PrivetHandlerTest() {}

 protected:
  void SetUp() override {
    auth_header_ = "Privet anonymous";
    handler_.reset(
        new PrivetHandler(&cloud_, &device_, &security_, &wifi_, &identity_));
  }

  const base::DictionaryValue& HandleRequest(
      const std::string& api,
      const base::DictionaryValue* input) {
    output_.Clear();
    handler_->HandleRequest(api, auth_header_, input,
                            base::Bind(&PrivetHandlerTest::HandlerCallback,
                                       base::Unretained(this)));
    base::RunLoop().RunUntilIdle();
    return output_;
  }

  const base::DictionaryValue& HandleRequest(const std::string& api,
                                             const std::string& json_input) {
    base::DictionaryValue dictionary;
    LoadTestJson(json_input, &dictionary);
    return HandleRequest(api, &dictionary);
  }

  void HandleUnknownRequest(const std::string& api) {
    output_.Clear();
    base::DictionaryValue dictionary;
    handler_->HandleRequest(api, auth_header_, &dictionary,
                            base::Bind(&PrivetHandlerTest::HandlerNoFound));
    base::RunLoop().RunUntilIdle();
  }

  void SetNoWifiAndGcd() {
    handler_.reset(
        new PrivetHandler(&cloud_, &device_, &security_, nullptr, &identity_));
    EXPECT_CALL(cloud_, GetCloudId()).WillRepeatedly(Return(""));
    EXPECT_CALL(cloud_, GetConnectionState())
        .WillRepeatedly(ReturnRef(gcd_disabled_state_));
    auto set_error =
        [](const std::string&, const std::string&, chromeos::ErrorPtr* error) {
          chromeos::Error::AddTo(error, FROM_HERE, errors::kDomain,
                                 "setupUnavailable", "");
        };
    EXPECT_CALL(cloud_, Setup(_, _, _))
        .WillRepeatedly(DoAll(Invoke(set_error), Return(false)));
  }

  testing::StrictMock<MockCloudDelegate> cloud_;
  testing::StrictMock<MockDeviceDelegate> device_;
  testing::StrictMock<MockSecurityDelegate> security_;
  testing::StrictMock<MockWifiDelegate> wifi_;
  testing::StrictMock<MockIdentityDelegate> identity_;
  std::string auth_header_;

 private:
  void HandlerCallback(int status, const base::DictionaryValue& output) {
    output_.MergeDictionary(&output);
    if (!output_.HasKey("error")) {
      EXPECT_EQ(chromeos::http::status_code::Ok, status);
      return;
    }
    EXPECT_NE(chromeos::http::status_code::Ok, status);
    output_.SetInteger("error.http_status", status);
  }

  static void HandlerNoFound(int status, const base::DictionaryValue&) {
    EXPECT_EQ(status, 404);
  }

  base::MessageLoop message_loop_;
  std::unique_ptr<PrivetHandler> handler_;
  base::DictionaryValue output_;
  ConnectionState gcd_disabled_state_{ConnectionState::kDisabled};
};

TEST_F(PrivetHandlerTest, UnknownApi) {
  HandleUnknownRequest("/privet/foo");
}

TEST_F(PrivetHandlerTest, InvalidFormat) {
  auth_header_ = "";
  EXPECT_PRED2(IsEqualError, CodeWithReason(400, "invalidFormat"),
               HandleRequest("/privet/info", nullptr));
}

TEST_F(PrivetHandlerTest, MissingAuth) {
  auth_header_ = "";
  EXPECT_PRED2(IsEqualError, CodeWithReason(401, "missingAuthorization"),
               HandleRequest("/privet/info", "{}"));
}

TEST_F(PrivetHandlerTest, InvalidAuth) {
  auth_header_ = "foo";
  EXPECT_PRED2(IsEqualError, CodeWithReason(401, "invalidAuthorization"),
               HandleRequest("/privet/info", "{}"));
}

TEST_F(PrivetHandlerTest, InvalidAuthScope) {
  EXPECT_PRED2(IsEqualError, CodeWithReason(403, "invalidAuthorizationScope"),
               HandleRequest("/privet/v3/setup/start", "{}"));
}

TEST_F(PrivetHandlerTest, InfoMinimal) {
  SetNoWifiAndGcd();
  EXPECT_CALL(security_, GetPairingTypes())
      .WillRepeatedly(Return(std::set<PairingType>{}));
  EXPECT_CALL(security_, GetCryptoTypes())
      .WillRepeatedly(Return(std::set<CryptoType>{}));

  const char kExpected[] = R"({
    'version': '3.0',
    'id': 'TestId',
    'name': 'TestDevice',
    'services': [],
    'modelManifestId': "ABMID",
    'basicModelManifest': {
      'uiDeviceKind': 'developmentBoard',
      'oemName': 'Chromium',
      'modelName': 'Brillo'
    },
    'endpoints': {
      'httpPort': 0,
      'httpUpdatesPort': 0,
      'httpsPort': 0,
      'httpsUpdatesPort': 0
    },
    'authentication': {
      'anonymousMaxScope': 'user',
      'mode': [
        'anonymous',
        'pairing'
      ],
      'pairing': [
      ],
      'crypto': [
      ]
    },
    'gcd': {
      'id': '',
      'status': 'disabled'
    },
    'uptime': 3600
  })";
  EXPECT_PRED2(IsEqualJson, kExpected, HandleRequest("/privet/info", "{}"));
}

TEST_F(PrivetHandlerTest, Info) {
  EXPECT_CALL(cloud_, GetDescription())
      .WillRepeatedly(Return("TestDescription"));
  EXPECT_CALL(cloud_, GetLocation()).WillRepeatedly(Return("TestLocation"));
  EXPECT_CALL(cloud_, GetServices())
      .WillRepeatedly(Return(std::set<std::string>{"service1", "service2"}));
  EXPECT_CALL(device_, GetHttpEnpoint())
      .WillRepeatedly(Return(std::make_pair(80, 10080)));
  EXPECT_CALL(device_, GetHttpsEnpoint())
      .WillRepeatedly(Return(std::make_pair(443, 10443)));
  EXPECT_CALL(wifi_, GetHostedSsid())
      .WillRepeatedly(Return("Test_device.BBABCLAprv"));

  const char kExpected[] = R"({
    'version': '3.0',
    'id': 'TestId',
    'name': 'TestDevice',
    'description': 'TestDescription',
    'location': 'TestLocation',
    'services': [
      "service1",
      "service2"
    ],
    'modelManifestId': "ABMID",
    'basicModelManifest': {
      'uiDeviceKind': 'developmentBoard',
      'oemName': 'Chromium',
      'modelName': 'Brillo'
    },
    'endpoints': {
      'httpPort': 80,
      'httpUpdatesPort': 10080,
      'httpsPort': 443,
      'httpsUpdatesPort': 10443
    },
    'authentication': {
      'anonymousMaxScope': 'guest',
      'mode': [
        'anonymous',
        'pairing'
      ],
      'pairing': [
        'pinCode',
        'embeddedCode',
        'ultrasound32',
        'audible32'
      ],
      'crypto': [
        'p224_spake2',
        'p256_spake2'
      ]
    },
    'wifi': {
      'capabilities': [
        '2.4GHz'
      ],
      'ssid': 'TestSsid',
      'hostedSsid': 'Test_device.BBABCLAprv',
      'status': 'offline'
    },
    'gcd': {
      'id': 'TestCloudId',
      'status': 'online'
    },
    'uptime': 3600
  })";
  EXPECT_PRED2(IsEqualJson, kExpected, HandleRequest("/privet/info", "{}"));
}

TEST_F(PrivetHandlerTest, PairingStartInvalidParams) {
  EXPECT_PRED2(IsEqualError, CodeWithReason(400, "invalidParams"),
               HandleRequest("/privet/v3/pairing/start",
                             "{'pairing':'embeddedCode','crypto':'crypto'}"));

  EXPECT_PRED2(IsEqualError, CodeWithReason(400, "invalidParams"),
               HandleRequest("/privet/v3/pairing/start",
                             "{'pairing':'code','crypto':'p256_spake2'}"));
}

TEST_F(PrivetHandlerTest, PairingStart) {
  EXPECT_PRED2(
      IsEqualJson,
      "{'deviceCommitment': 'testCommitment', 'sessionId': 'testSession'}",
      HandleRequest("/privet/v3/pairing/start",
                    "{'pairing': 'embeddedCode', 'crypto': 'p256_spake2'}"));
}

TEST_F(PrivetHandlerTest, PairingConfirm) {
  EXPECT_PRED2(
      IsEqualJson,
      "{'certFingerprint':'testFingerprint','certSignature':'testSignature'}",
      HandleRequest(
          "/privet/v3/pairing/confirm",
          "{'sessionId':'testSession','clientCommitment':'testCommitment'}"));
}

TEST_F(PrivetHandlerTest, PairingCancel) {
  EXPECT_PRED2(IsEqualJson, "{}",
               HandleRequest("/privet/v3/pairing/cancel",
                             "{'sessionId': 'testSession'}"));
}

TEST_F(PrivetHandlerTest, AuthErrorNoType) {
  EXPECT_PRED2(IsEqualError, CodeWithReason(400, "invalidAuthMode"),
               HandleRequest("/privet/v3/auth", "{}"));
}

TEST_F(PrivetHandlerTest, AuthErrorInvalidType) {
  EXPECT_PRED2(IsEqualError, CodeWithReason(400, "invalidAuthMode"),
               HandleRequest("/privet/v3/auth", "{'mode':'unknown'}"));
}

TEST_F(PrivetHandlerTest, AuthErrorNoScope) {
  EXPECT_PRED2(IsEqualError, CodeWithReason(400, "invalidRequestedScope"),
               HandleRequest("/privet/v3/auth", "{'mode':'anonymous'}"));
}

TEST_F(PrivetHandlerTest, AuthErrorInvalidScope) {
  EXPECT_PRED2(
      IsEqualError, CodeWithReason(400, "invalidRequestedScope"),
      HandleRequest("/privet/v3/auth",
                    "{'mode':'anonymous','requestedScope':'unknown'}"));
}

TEST_F(PrivetHandlerTest, AuthErrorAccessDenied) {
  EXPECT_PRED2(IsEqualError, CodeWithReason(403, "accessDenied"),
               HandleRequest("/privet/v3/auth",
                             "{'mode':'anonymous','requestedScope':'owner'}"));
}

TEST_F(PrivetHandlerTest, AuthErrorInvalidAuthCode) {
  EXPECT_CALL(security_, IsValidPairingCode("testToken"))
      .WillRepeatedly(Return(false));
  const char kInput[] = R"({
    'mode': 'pairing',
    'requestedScope': 'user',
    'authCode': 'testToken'
  })";
  EXPECT_PRED2(IsEqualError, CodeWithReason(403, "invalidAuthCode"),
               HandleRequest("/privet/v3/auth", kInput));
}

TEST_F(PrivetHandlerTest, AuthAnonymous) {
  const char kExpected[] = R"({
    'accessToken': 'GuestAccessToken',
    'expiresIn': 3600,
    'scope': 'user',
    'tokenType': 'Privet'
  })";
  EXPECT_PRED2(IsEqualJson, kExpected,
               HandleRequest("/privet/v3/auth",
                             "{'mode':'anonymous','requestedScope':'auto'}"));
}

TEST_F(PrivetHandlerTest, AuthPairing) {
  EXPECT_CALL(security_, IsValidPairingCode("testToken"))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(security_, CreateAccessToken(_, _))
      .WillRepeatedly(Return("OwnerAccessToken"));
  const char kInput[] = R"({
    'mode': 'pairing',
    'requestedScope': 'owner',
    'authCode': 'testToken'
  })";
  const char kExpected[] = R"({
    'accessToken': 'OwnerAccessToken',
    'expiresIn': 3600,
    'scope': 'owner',
    'tokenType': 'Privet'
  })";
  EXPECT_PRED2(
      IsEqualJson, kExpected, HandleRequest("/privet/v3/auth", kInput));
}

class PrivetHandlerSetupTest : public PrivetHandlerTest {
 public:
  void SetUp() override {
    PrivetHandlerTest::SetUp();
    auth_header_ = "Privet 123";
    EXPECT_CALL(security_, ParseAccessToken(_, _))
        .WillRepeatedly(DoAll(SetArgPointee<1>(base::Time::Now()),
                              Return(AuthScope::kOwner)));
  }
};

TEST_F(PrivetHandlerSetupTest, StatusEmpty) {
  SetNoWifiAndGcd();
  EXPECT_PRED2(
      IsEqualJson, "{}", HandleRequest("/privet/v3/setup/status", "{}"));
}

TEST_F(PrivetHandlerSetupTest, StatusWifi) {
  wifi_.setup_state_ = SetupState{SetupState::kSuccess};

  const char kExpected[] = R"({
    'wifi': {
        'ssid': 'TestSsid',
        'status': 'success'
     }
  })";
  EXPECT_PRED2(
      IsEqualJson, kExpected, HandleRequest("/privet/v3/setup/status", "{}"));
}

TEST_F(PrivetHandlerSetupTest, StatusWifiError) {
  chromeos::ErrorPtr error;
  chromeos::Error::AddTo(&error, FROM_HERE, "test", "invalidPassphrase", "");
  wifi_.setup_state_ = SetupState{std::move(error)};

  const char kExpected[] = R"({
    'wifi': {
        'status': 'error',
        'error': {
          'code': 'invalidPassphrase'
        }
     }
  })";
  EXPECT_PRED2(
      IsEqualJson, kExpected, HandleRequest("/privet/v3/setup/status", "{}"));
}

TEST_F(PrivetHandlerSetupTest, StatusGcd) {
  cloud_.setup_state_ = SetupState{SetupState::kSuccess};

  const char kExpected[] = R"({
    'gcd': {
        'id': 'TestCloudId',
        'status': 'success'
     }
  })";
  EXPECT_PRED2(
      IsEqualJson, kExpected, HandleRequest("/privet/v3/setup/status", "{}"));
}

TEST_F(PrivetHandlerSetupTest, StatusGcdError) {
  chromeos::ErrorPtr error;
  chromeos::Error::AddTo(&error, FROM_HERE, "test", "invalidTicket", "");
  cloud_.setup_state_ = SetupState{std::move(error)};

  const char kExpected[] = R"({
    'gcd': {
        'status': 'error',
        'error': {
          'code': 'invalidTicket'
        }
     }
  })";
  EXPECT_PRED2(
      IsEqualJson, kExpected, HandleRequest("/privet/v3/setup/status", "{}"));
}

TEST_F(PrivetHandlerSetupTest, SetupNameDescriptionLocation) {
  EXPECT_CALL(cloud_, UpdateDeviceInfo("testName", "testDescription",
                                       "testLocation", _, _)).Times(1);
  const char kInput[] = R"({
    'name': 'testName',
    'description': 'testDescription',
    'location': 'testLocation'
  })";
  EXPECT_PRED2(IsEqualJson, "{}",
               HandleRequest("/privet/v3/setup/start", kInput));
}

TEST_F(PrivetHandlerSetupTest, InvalidParams) {
  const char kInputWifi[] = R"({
    'wifi': {
      'ssid': ''
    }
  })";
  EXPECT_PRED2(IsEqualError, CodeWithReason(400, "invalidParams"),
               HandleRequest("/privet/v3/setup/start", kInputWifi));

  const char kInputRegistration[] = R"({
    'gcd': {
      'ticketId': ''
    }
  })";
  EXPECT_PRED2(IsEqualError, CodeWithReason(400, "invalidParams"),
               HandleRequest("/privet/v3/setup/start", kInputRegistration));
}

TEST_F(PrivetHandlerSetupTest, WifiSetupUnavailable) {
  SetNoWifiAndGcd();
  EXPECT_PRED2(IsEqualError, CodeWithReason(400, "setupUnavailable"),
               HandleRequest("/privet/v3/setup/start", "{'wifi': {}}"));
}

TEST_F(PrivetHandlerSetupTest, WifiSetup) {
  const char kInput[] = R"({
    'wifi': {
      'ssid': 'testSsid',
      'passphrase': 'testPass'
    }
  })";
  auto set_error = [](const std::string&, const std::string&,
                      chromeos::ErrorPtr* error) {
    chromeos::Error::AddTo(error, FROM_HERE, errors::kDomain, "deviceBusy", "");
  };
  EXPECT_CALL(wifi_, ConfigureCredentials(_, _, _))
      .WillOnce(DoAll(Invoke(set_error), Return(false)));
  EXPECT_PRED2(IsEqualError, CodeWithReason(503, "deviceBusy"),
               HandleRequest("/privet/v3/setup/start", kInput));

  const char kExpected[] = R"({
    'wifi': {
      'status': 'inProgress'
    }
  })";
  wifi_.setup_state_ = SetupState{SetupState::kInProgress};
  EXPECT_CALL(wifi_, ConfigureCredentials("testSsid", "testPass", _))
      .WillOnce(Return(true));
  EXPECT_PRED2(IsEqualJson, kExpected,
               HandleRequest("/privet/v3/setup/start", kInput));
}

TEST_F(PrivetHandlerSetupTest, GcdSetupUnavailable) {
  SetNoWifiAndGcd();
  const char kInput[] = R"({
    'gcd': {
      'ticketId': 'testTicket',
      'user': 'testUser'
    }
  })";

  EXPECT_PRED2(IsEqualError, CodeWithReason(400, "setupUnavailable"),
               HandleRequest("/privet/v3/setup/start", kInput));
}

TEST_F(PrivetHandlerSetupTest, GcdSetup) {
  const char kInput[] = R"({
    'gcd': {
      'ticketId': 'testTicket',
      'user': 'testUser'
    }
  })";

  auto set_error = [](const std::string&, const std::string&,
                      chromeos::ErrorPtr* error) {
    chromeos::Error::AddTo(error, FROM_HERE, errors::kDomain, "deviceBusy", "");
  };
  EXPECT_CALL(cloud_, Setup(_, _, _))
      .WillOnce(DoAll(Invoke(set_error), Return(false)));
  EXPECT_PRED2(IsEqualError, CodeWithReason(503, "deviceBusy"),
               HandleRequest("/privet/v3/setup/start", kInput));

  const char kExpected[] = R"({
    'gcd': {
      'status': 'inProgress'
    }
  })";
  cloud_.setup_state_ = SetupState{SetupState::kInProgress};
  EXPECT_CALL(cloud_, Setup("testTicket", "testUser", _))
      .WillOnce(Return(true));
  EXPECT_PRED2(IsEqualJson, kExpected,
               HandleRequest("/privet/v3/setup/start", kInput));
}

TEST_F(PrivetHandlerSetupTest, State) {
  EXPECT_PRED2(IsEqualJson, "{'state': {'test': {}}, 'fingerprint': '0'}",
               HandleRequest("/privet/v3/state", "{}"));

  cloud_.NotifyOnStateChanged();

  EXPECT_PRED2(IsEqualJson, "{'state': {'test': {}}, 'fingerprint': '1'}",
               HandleRequest("/privet/v3/state", "{}"));
}

TEST_F(PrivetHandlerSetupTest, CommandsDefs) {
  EXPECT_PRED2(IsEqualJson, "{'commands': {'test':{}}, 'fingerprint': '0'}",
               HandleRequest("/privet/v3/commandDefs", "{}"));

  cloud_.NotifyOnCommandDefsChanged();

  EXPECT_PRED2(IsEqualJson, "{'commands': {'test':{}}, 'fingerprint': '1'}",
               HandleRequest("/privet/v3/commandDefs", "{}"));
}

TEST_F(PrivetHandlerSetupTest, CommandsExecute) {
  const char kInput[] = "{'name': 'test'}";
  base::DictionaryValue command;
  LoadTestJson(kInput, &command);
  LoadTestJson("{'id':'5'}", &command);
  EXPECT_CALL(cloud_, AddCommand(_, _, _))
      .WillOnce(RunCallback<1, const base::DictionaryValue&>(command));

  EXPECT_PRED2(IsEqualJson, "{'name':'test', 'id':'5'}",
               HandleRequest("/privet/v3/commands/execute", kInput));
}

TEST_F(PrivetHandlerSetupTest, CommandsStatus) {
  const char kInput[] = "{'id': '5'}";
  base::DictionaryValue command;
  LoadTestJson(kInput, &command);
  LoadTestJson("{'name':'test'}", &command);
  EXPECT_CALL(cloud_, GetCommand(_, _, _))
      .WillOnce(RunCallback<1, const base::DictionaryValue&>(command));

  EXPECT_PRED2(IsEqualJson, "{'name':'test', 'id':'5'}",
               HandleRequest("/privet/v3/commands/status", kInput));

  chromeos::ErrorPtr error;
  chromeos::Error::AddTo(&error, FROM_HERE, errors::kDomain, "notFound", "");
  EXPECT_CALL(cloud_, GetCommand(_, _, _))
      .WillOnce(RunCallback<2>(error.get()));

  EXPECT_PRED2(IsEqualError, CodeWithReason(404, "notFound"),
               HandleRequest("/privet/v3/commands/status", "{'id': '15'}"));
}

TEST_F(PrivetHandlerSetupTest, CommandsCancel) {
  const char kExpected[] = "{'id': '5', 'name':'test', 'state':'cancelled'}";
  base::DictionaryValue command;
  LoadTestJson(kExpected, &command);
  EXPECT_CALL(cloud_, CancelCommand(_, _, _))
      .WillOnce(RunCallback<1, const base::DictionaryValue&>(command));

  EXPECT_PRED2(IsEqualJson, kExpected,
               HandleRequest("/privet/v3/commands/cancel", "{'id': '8'}"));

  chromeos::ErrorPtr error;
  chromeos::Error::AddTo(&error, FROM_HERE, errors::kDomain, "notFound", "");
  EXPECT_CALL(cloud_, CancelCommand(_, _, _))
      .WillOnce(RunCallback<2>(error.get()));

  EXPECT_PRED2(IsEqualError, CodeWithReason(404, "notFound"),
               HandleRequest("/privet/v3/commands/cancel", "{'id': '11'}"));
}

TEST_F(PrivetHandlerSetupTest, CommandsList) {
  const char kExpected[] = R"({
    'commands' : [
        {'id':'5', 'state':'cancelled'},
        {'id':'15', 'state':'inProgress'}
     ]})";

  base::DictionaryValue commands;
  LoadTestJson(kExpected, &commands);

  EXPECT_CALL(cloud_, ListCommands(_, _))
      .WillOnce(RunCallback<0, const base::DictionaryValue&>(commands));

  EXPECT_PRED2(IsEqualJson, kExpected,
               HandleRequest("/privet/v3/commands/list", "{}"));
}

}  // namespace privetd
