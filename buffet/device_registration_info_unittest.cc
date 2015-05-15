// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "buffet/device_registration_info.h"

#include <base/json/json_reader.h>
#include <base/json/json_writer.h>
#include <base/values.h>
#include <chromeos/bind_lambda.h>
#include <chromeos/http/http_request.h>
#include <chromeos/http/http_transport_fake.h>
#include <chromeos/key_value_store.h>
#include <chromeos/mime_utils.h>
#include <gtest/gtest.h>

#include "buffet/commands/command_manager.h"
#include "buffet/commands/unittest_utils.h"
#include "buffet/states/mock_state_change_queue_interface.h"
#include "buffet/states/state_manager.h"
#include "buffet/storage_impls.h"

namespace buffet {

using chromeos::http::request_header::kAuthorization;
using chromeos::http::fake::ServerRequest;
using chromeos::http::fake::ServerResponse;

namespace {

namespace test_data {

const char kServiceURL[]           = "http://gcd.server.com/";
const char kOAuthURL[]             = "http://oauth.server.com/";
const char kApiKey[]               = "GOadRdTf9FERf0k4w6EFOof56fUJ3kFDdFL3d7f";
const char kClientId[]             = "123543821385-sfjkjshdkjhfk234sdfsdfkskd"
                                     "fkjh7f.apps.googleusercontent.com";
const char kClientSecret[]         = "5sdGdGlfolGlrFKfdFlgP6FG";
const char kDeviceId[]             = "4a7ea2d1-b331-1e1f-b206-e863c7635196";
const char kClaimTicketId[]        = "RTcUE";
const char kAccessToken[]          = "ya29.1.AADtN_V-dLUM-sVZ0qVjG9Dxm5NgdS9J"
                                     "Mx_JLUqhC9bED_YFjzHZtYt65ZzXCS35NMAeaVZ"
                                     "Dei530-w0yE2urpQ";
const char kRefreshToken[]         = "1/zQmxR6PKNvhcxf9SjXUrCjcmCrcqRKXctc6cp"
                                     "1nI-GQ";
const char kRobotAccountAuthCode[] = "4/Mf_ujEhPejVhOq-OxW9F5cSOnWzx."
                                     "YgciVjTYGscRshQV0ieZDAqiTIjMigI";
const char kRobotAccountEmail[]    = "6ed0b3f54f9bd619b942f4ad2441c252@"
                                     "clouddevices.gserviceaccount.com";
const char kUserAccountAuthCode[]  = "2/sd_GD1TGFKpJOLJ34-0g5fK0fflp.GlT"
                                     "I0F5g7hNtFgj5HFGOf8FlGK9eflO";
const char kUserAccessToken[]      = "sd56.4.FGDjG_F-gFGF-dFG6gGOG9Dxm5NgdS9"
                                     "JMx_JLUqhC9bED_YFjLKjlkjLKJlkjLKjlKJea"
                                     "VZDei530-w0yE2urpQ";
const char kUserRefreshToken[]     = "1/zQLKjlKJlkLkLKjLkjLKjLkjLjLkjl0ftc6"
                                     "cp1nI-GQ";
}  // namespace test_data

// Add the test device registration information.
void SetDefaultDeviceRegistration(base::DictionaryValue* data) {
  data->SetString("refresh_token", test_data::kRefreshToken);
  data->SetString("device_id", test_data::kDeviceId);
  data->SetString("robot_account", test_data::kRobotAccountEmail);
}

void OAuth2Handler(const ServerRequest& request, ServerResponse* response) {
  base::DictionaryValue json;
  if (request.GetFormField("grant_type") == "refresh_token") {
    // Refresh device access token.
    EXPECT_EQ(test_data::kRefreshToken, request.GetFormField("refresh_token"));
    EXPECT_EQ(test_data::kClientId, request.GetFormField("client_id"));
    EXPECT_EQ(test_data::kClientSecret, request.GetFormField("client_secret"));
    json.SetString("access_token", test_data::kAccessToken);
  } else if (request.GetFormField("grant_type") == "authorization_code") {
    // Obtain access token.
    std::string code = request.GetFormField("code");
    if (code == test_data::kUserAccountAuthCode) {
      // Get user access token.
      EXPECT_EQ(test_data::kClientId, request.GetFormField("client_id"));
      EXPECT_EQ(test_data::kClientSecret,
                request.GetFormField("client_secret"));
      EXPECT_EQ("urn:ietf:wg:oauth:2.0:oob",
                request.GetFormField("redirect_uri"));
      json.SetString("access_token", test_data::kUserAccessToken);
      json.SetString("token_type", "Bearer");
      json.SetString("refresh_token", test_data::kUserRefreshToken);
    } else if (code == test_data::kRobotAccountAuthCode) {
      // Get device access token.
      EXPECT_EQ(test_data::kClientId, request.GetFormField("client_id"));
      EXPECT_EQ(test_data::kClientSecret,
                request.GetFormField("client_secret"));
      EXPECT_EQ("oob", request.GetFormField("redirect_uri"));
      EXPECT_EQ("https://www.googleapis.com/auth/clouddevices",
                request.GetFormField("scope"));
      json.SetString("access_token", test_data::kAccessToken);
      json.SetString("token_type", "Bearer");
      json.SetString("refresh_token", test_data::kRefreshToken);
    } else {
      FAIL() << "Unexpected authorization code";
    }
  } else {
    FAIL() << "Unexpected grant type";
  }
  json.SetInteger("expires_in", 3600);
  response->ReplyJson(chromeos::http::status_code::Ok, &json);
}

void OAuth2HandlerFail(const ServerRequest& request,
                       ServerResponse* response) {
  base::DictionaryValue json;
  EXPECT_EQ("refresh_token", request.GetFormField("grant_type"));
  EXPECT_EQ(test_data::kRefreshToken, request.GetFormField("refresh_token"));
  EXPECT_EQ(test_data::kClientId, request.GetFormField("client_id"));
  EXPECT_EQ(test_data::kClientSecret, request.GetFormField("client_secret"));
  json.SetString("error", "unable_to_authenticate");
  response->ReplyJson(chromeos::http::status_code::BadRequest, &json);
}

void OAuth2HandlerDeregister(const ServerRequest& request,
                             ServerResponse* response) {
  base::DictionaryValue json;
  EXPECT_EQ("refresh_token", request.GetFormField("grant_type"));
  EXPECT_EQ(test_data::kRefreshToken, request.GetFormField("refresh_token"));
  EXPECT_EQ(test_data::kClientId, request.GetFormField("client_id"));
  EXPECT_EQ(test_data::kClientSecret, request.GetFormField("client_secret"));
  json.SetString("error", "invalid_grant");
  response->ReplyJson(chromeos::http::status_code::BadRequest, &json);
}

void DeviceInfoHandler(const ServerRequest& request, ServerResponse* response) {
  std::string auth = "Bearer ";
  auth += test_data::kAccessToken;
  EXPECT_EQ(auth,
            request.GetHeader(chromeos::http::request_header::kAuthorization));
  response->ReplyJson(chromeos::http::status_code::Ok, {
    {"channel.supportedType", "xmpp"},
    {"deviceKind", "vendor"},
    {"id", test_data::kDeviceId},
    {"kind", "clouddevices#device"},
  });
}

void FinalizeTicketHandler(const ServerRequest& request,
                           ServerResponse* response) {
  EXPECT_EQ(test_data::kApiKey, request.GetFormField("key"));
  EXPECT_TRUE(request.GetData().empty());

  response->ReplyJson(chromeos::http::status_code::Ok, {
    {"id", test_data::kClaimTicketId},
    {"kind", "clouddevices#registrationTicket"},
    {"oauthClientId", test_data::kClientId},
    {"userEmail", "user@email.com"},
    {"deviceDraft.id", test_data::kDeviceId},
    {"deviceDraft.kind", "clouddevices#device"},
    {"deviceDraft.channel.supportedType", "xmpp"},
    {"robotAccountEmail", test_data::kRobotAccountEmail},
    {"robotAccountAuthorizationCode", test_data::kRobotAccountAuthCode},
  });
}

}  // anonymous namespace

class DeviceRegistrationInfoTest : public ::testing::Test {
 protected:
  void SetUp() override {
    std::unique_ptr<StorageInterface> storage{new MemStorage};
    storage_ = storage.get();
    storage->Save(data_);
    transport_ = std::make_shared<chromeos::http::fake::Transport>();
    command_manager_ = std::make_shared<CommandManager>();
    state_manager_ = std::make_shared<StateManager>(&mock_state_change_queue_);

    std::unique_ptr<BuffetConfig> config{new BuffetConfig{std::move(storage)}};
    config_ = config.get();
    dev_reg_ = std::unique_ptr<DeviceRegistrationInfo>(
        new DeviceRegistrationInfo(command_manager_, state_manager_,
                                   std::move(config), transport_, true));

    ReloadConfig();
  }

  void ReloadConfig() {
    chromeos::KeyValueStore config_store;
    config_store.SetString("client_id", test_data::kClientId);
    config_store.SetString("client_secret", test_data::kClientSecret);
    config_store.SetString("api_key", test_data::kApiKey);
    config_store.SetString("device_kind",  "vendor");
    config_store.SetString("name",  "Coffee Pot");
    config_store.SetString("description", "Easy to clean");
    config_store.SetString("location", "Kitchen");
    config_store.SetString("local_anonymous_access_role", "viewer");
    config_store.SetString("model_id", "AAAAA");
    config_store.SetString("oauth_url", test_data::kOAuthURL);
    config_store.SetString("service_url", test_data::kServiceURL);
    config_->Load(config_store);
    dev_reg_->Start();
  }

  void PublishCommands(const base::ListValue& commands) {
    return dev_reg_->PublishCommands(commands);
  }

  bool CheckRegistration(chromeos::ErrorPtr* error) const {
    return dev_reg_->CheckRegistration(error);
  }

  RegistrationStatus GetRegistrationStatus() const {
    return dev_reg_->registration_status_;
  }

  base::DictionaryValue data_;
  StorageInterface* storage_{nullptr};
  BuffetConfig* config_{nullptr};
  std::shared_ptr<chromeos::http::fake::Transport> transport_;
  std::unique_ptr<DeviceRegistrationInfo> dev_reg_;
  std::shared_ptr<CommandManager> command_manager_;
  testing::NiceMock<MockStateChangeQueueInterface> mock_state_change_queue_;
  std::shared_ptr<StateManager> state_manager_;
};

////////////////////////////////////////////////////////////////////////////////
TEST_F(DeviceRegistrationInfoTest, GetServiceURL) {
  EXPECT_EQ(test_data::kServiceURL, dev_reg_->GetServiceURL());
  std::string url = test_data::kServiceURL;
  url += "registrationTickets";
  EXPECT_EQ(url, dev_reg_->GetServiceURL("registrationTickets"));
  url += "?key=";
  url += test_data::kApiKey;
  EXPECT_EQ(url, dev_reg_->GetServiceURL("registrationTickets", {
    {"key", test_data::kApiKey}
  }));
  url += "&restart=true";
  EXPECT_EQ(url, dev_reg_->GetServiceURL("registrationTickets", {
    {"key", test_data::kApiKey},
    {"restart", "true"},
  }));
}

TEST_F(DeviceRegistrationInfoTest, GetOAuthURL) {
  EXPECT_EQ(test_data::kOAuthURL, dev_reg_->GetOAuthURL());
  std::string url = test_data::kOAuthURL;
  url += "auth?scope=https%3A%2F%2Fwww.googleapis.com%2Fauth%2Fclouddevices&";
  url += "redirect_uri=urn%3Aietf%3Awg%3Aoauth%3A2.0%3Aoob&";
  url += "response_type=code&";
  url += "client_id=";
  url += test_data::kClientId;
  EXPECT_EQ(url, dev_reg_->GetOAuthURL("auth", {
    {"scope", "https://www.googleapis.com/auth/clouddevices"},
    {"redirect_uri", "urn:ietf:wg:oauth:2.0:oob"},
    {"response_type", "code"},
    {"client_id", test_data::kClientId}
  }));
}

TEST_F(DeviceRegistrationInfoTest, CheckRegistration) {
  EXPECT_FALSE(CheckRegistration(nullptr));
  EXPECT_EQ(0, transport_->GetRequestCount());

  SetDefaultDeviceRegistration(&data_);
  storage_->Save(data_);
  ReloadConfig();

  transport_->AddHandler(dev_reg_->GetOAuthURL("token"),
                         chromeos::http::request_type::kPost,
                         base::Bind(OAuth2Handler));
  transport_->ResetRequestCount();
  EXPECT_TRUE(CheckRegistration(nullptr));
  EXPECT_EQ(1, transport_->GetRequestCount());
}

TEST_F(DeviceRegistrationInfoTest, CheckAuthenticationFailure) {
  SetDefaultDeviceRegistration(&data_);
  storage_->Save(data_);
  ReloadConfig();
  EXPECT_EQ(RegistrationStatus::kConnecting, GetRegistrationStatus());

  transport_->AddHandler(dev_reg_->GetOAuthURL("token"),
                         chromeos::http::request_type::kPost,
                         base::Bind(OAuth2HandlerFail));
  transport_->ResetRequestCount();
  chromeos::ErrorPtr error;
  EXPECT_FALSE(CheckRegistration(&error));
  EXPECT_EQ(1, transport_->GetRequestCount());
  EXPECT_TRUE(error->HasError(kErrorDomainOAuth2, "unable_to_authenticate"));
  EXPECT_EQ(RegistrationStatus::kConnecting, GetRegistrationStatus());
}

TEST_F(DeviceRegistrationInfoTest, CheckDeregistration) {
  SetDefaultDeviceRegistration(&data_);
  storage_->Save(data_);
  ReloadConfig();
  EXPECT_EQ(RegistrationStatus::kConnecting, GetRegistrationStatus());

  transport_->AddHandler(dev_reg_->GetOAuthURL("token"),
                         chromeos::http::request_type::kPost,
                         base::Bind(OAuth2HandlerDeregister));
  transport_->ResetRequestCount();
  chromeos::ErrorPtr error;
  EXPECT_FALSE(CheckRegistration(&error));
  EXPECT_EQ(1, transport_->GetRequestCount());
  EXPECT_TRUE(error->HasError(kErrorDomainOAuth2, "invalid_grant"));
  EXPECT_EQ(RegistrationStatus::kInvalidCredentials, GetRegistrationStatus());
}

TEST_F(DeviceRegistrationInfoTest, GetDeviceInfo) {
  SetDefaultDeviceRegistration(&data_);
  storage_->Save(data_);
  ReloadConfig();

  transport_->AddHandler(dev_reg_->GetOAuthURL("token"),
                         chromeos::http::request_type::kPost,
                         base::Bind(OAuth2Handler));
  transport_->AddHandler(dev_reg_->GetDeviceURL(),
                         chromeos::http::request_type::kGet,
                         base::Bind(DeviceInfoHandler));
  transport_->ResetRequestCount();
  auto device_info = dev_reg_->GetDeviceInfo(nullptr);
  EXPECT_EQ(2, transport_->GetRequestCount());
  EXPECT_NE(nullptr, device_info.get());
  base::DictionaryValue* dict = nullptr;
  EXPECT_TRUE(device_info->GetAsDictionary(&dict));
  std::string id;
  EXPECT_TRUE(dict->GetString("id", &id));
  EXPECT_EQ(test_data::kDeviceId, id);
}

TEST_F(DeviceRegistrationInfoTest, GetDeviceId) {
  SetDefaultDeviceRegistration(&data_);
  storage_->Save(data_);
  ReloadConfig();

  transport_->AddHandler(dev_reg_->GetOAuthURL("token"),
                         chromeos::http::request_type::kPost,
                         base::Bind(OAuth2Handler));
  transport_->AddHandler(dev_reg_->GetDeviceURL(),
                         chromeos::http::request_type::kGet,
                         base::Bind(DeviceInfoHandler));
  EXPECT_EQ(test_data::kDeviceId, config_->device_id());
}

TEST_F(DeviceRegistrationInfoTest, RegisterDevice) {
  auto update_ticket = [](const ServerRequest& request,
                          ServerResponse* response) {
    EXPECT_EQ(test_data::kApiKey, request.GetFormField("key"));
    auto json = request.GetDataAsJson();
    EXPECT_NE(nullptr, json.get());
    std::string value;
    EXPECT_TRUE(json->GetString("id", &value));
    EXPECT_EQ(test_data::kClaimTicketId, value);
    EXPECT_TRUE(json->GetString("deviceDraft.channel.supportedType", &value));
    EXPECT_EQ("xmpp", value);
    EXPECT_TRUE(json->GetString("oauthClientId", &value));
    EXPECT_EQ(test_data::kClientId, value);
    EXPECT_TRUE(json->GetString("deviceDraft.deviceKind", &value));
    EXPECT_EQ("vendor", value);
    EXPECT_TRUE(json->GetString("deviceDraft.description", &value));
    EXPECT_EQ("Easy to clean", value);
    EXPECT_TRUE(json->GetString("deviceDraft.location", &value));
    EXPECT_EQ("Kitchen", value);
    EXPECT_TRUE(json->GetString("deviceDraft.modelManifestId", &value));
    EXPECT_EQ("AAAAA", value);
    EXPECT_TRUE(json->GetString("deviceDraft.name", &value));
    EXPECT_EQ("Coffee Pot", value);
    base::DictionaryValue* commandDefs = nullptr;
    EXPECT_TRUE(json->GetDictionary("deviceDraft.commandDefs", &commandDefs));
    EXPECT_FALSE(commandDefs->empty());

    auto expected = R"({
      'base': {
        'reboot': {
          'parameters': {
            'delay': {
              'minimum': 10,
              'type': 'integer'
            }
          }
        }
      },
      'robot': {
        '_jump': {
          'parameters': {
            '_height': {
              'type': 'integer'
            }
          }
        }
      }
    })";
    EXPECT_JSON_EQ(expected, *commandDefs);

    base::DictionaryValue json_resp;
    json_resp.SetString("id", test_data::kClaimTicketId);
    json_resp.SetString("kind", "clouddevices#registrationTicket");
    json_resp.SetString("oauthClientId", test_data::kClientId);
    base::DictionaryValue* device_draft = nullptr;
    EXPECT_TRUE(json->GetDictionary("deviceDraft", &device_draft));
    device_draft = device_draft->DeepCopy();
    device_draft->SetString("id", test_data::kDeviceId);
    device_draft->SetString("kind", "clouddevices#device");
    json_resp.Set("deviceDraft", device_draft);

    response->ReplyJson(chromeos::http::status_code::Ok, &json_resp);
  };

  auto json_base = unittests::CreateDictionaryValue(R"({
    'base': {
      'reboot': {
        'parameters': {'delay': 'integer'},
        'results': {}
      },
      'shutdown': {
        'parameters': {},
        'results': {}
      }
    }
  })");
  EXPECT_TRUE(command_manager_->LoadBaseCommands(*json_base, nullptr));
  auto json_cmds = unittests::CreateDictionaryValue(R"({
    'base': {
      'reboot': {
        'parameters': {'delay': {'minimum': 10}},
        'results': {}
      }
    },
    'robot': {
      '_jump': {
        'parameters': {'_height': 'integer'},
        'results': {}
      }
    }
  })");
  EXPECT_TRUE(command_manager_->LoadCommands(*json_cmds, "", nullptr));

  transport_->AddHandler(dev_reg_->GetServiceURL(
      std::string("registrationTickets/") + test_data::kClaimTicketId),
      chromeos::http::request_type::kPatch,
      base::Bind(update_ticket));
  std::string ticket_url =
      dev_reg_->GetServiceURL("registrationTickets/" +
                             std::string(test_data::kClaimTicketId));
  transport_->AddHandler(ticket_url + "/finalize",
                         chromeos::http::request_type::kPost,
                         base::Bind(FinalizeTicketHandler));

  transport_->AddHandler(dev_reg_->GetOAuthURL("token"),
                         chromeos::http::request_type::kPost,
                         base::Bind(OAuth2Handler));
  std::string device_id =
      dev_reg_->RegisterDevice(test_data::kClaimTicketId, nullptr);

  EXPECT_EQ(test_data::kDeviceId, device_id);
  EXPECT_EQ(3, transport_->GetRequestCount());
  EXPECT_EQ(RegistrationStatus::kConnecting, GetRegistrationStatus());

  // Validate the device info saved to storage...
  auto storage_data = storage_->Load();
  base::DictionaryValue* dict = nullptr;
  EXPECT_TRUE(storage_data->GetAsDictionary(&dict));
  std::string value;
  EXPECT_TRUE(dict->GetString("device_id", &value));
  EXPECT_EQ(test_data::kDeviceId, value);
  EXPECT_TRUE(dict->GetString("refresh_token", &value));
  EXPECT_EQ(test_data::kRefreshToken, value);
  EXPECT_TRUE(dict->GetString("robot_account", &value));
  EXPECT_EQ(test_data::kRobotAccountEmail, value);
}

TEST_F(DeviceRegistrationInfoTest, OOBRegistrationStatus) {
  // After we've been initialized, we should be either offline or unregistered,
  // depending on whether or not we've found credentials.
  EXPECT_EQ(RegistrationStatus::kUnconfigured, GetRegistrationStatus());
  // Put some credentials into our state, make sure we call that offline.
  SetDefaultDeviceRegistration(&data_);
  storage_->Save(data_);
  ReloadConfig();
  EXPECT_EQ(RegistrationStatus::kConnecting, GetRegistrationStatus());
}

TEST_F(DeviceRegistrationInfoTest, UpdateCommand) {
  auto json_cmds = unittests::CreateDictionaryValue(R"({
    'robot': {
      '_jump': {
        'parameters': {'_height': 'integer'},
        'results': {'status': 'string'}
      }
    }
  })");
  EXPECT_TRUE(command_manager_->LoadCommands(*json_cmds, "", nullptr));

  const std::string command_url = dev_reg_->GetServiceURL("commands/1234");

  auto commands_json = unittests::CreateValue(R"([{
    'name':'robot._jump',
    'id':'1234',
    'parameters': {'_height': 100}
  }])");
  ASSERT_NE(nullptr, commands_json.get());
  const base::ListValue* command_list = nullptr;
  ASSERT_TRUE(commands_json->GetAsList(&command_list));
  PublishCommands(*command_list);
  auto command = command_manager_->FindCommand("1234");
  ASSERT_NE(nullptr, command);
  StringPropType string_type;
  native_types::Object results{
    {"status", string_type.CreateValue(std::string{"Ok"}, nullptr)}
  };

  // UpdateCommand when setting command results.
  auto update_command_results = [](const ServerRequest& request,
                                   ServerResponse* response) {
    EXPECT_EQ(R"({"results":{"status":"Ok"}})",
              request.GetDataAsNormalizedJsonString());
    response->ReplyJson(chromeos::http::status_code::Ok,
                        chromeos::http::FormFieldList{});
  };

  transport_->AddHandler(command_url,
                         chromeos::http::request_type::kPatch,
                         base::Bind(update_command_results));

  command->SetResults(results);

  // UpdateCommand when setting command progress.
  int count = 0;  // This will be called twice...
  auto update_command_progress = [&count](const ServerRequest& request,
                                          ServerResponse* response) {
    if (count++ == 0) {
      EXPECT_EQ(R"({"state":"inProgress"})",
                request.GetDataAsNormalizedJsonString());
    } else {
      EXPECT_EQ(R"({"progress":{"progress":18}})",
                request.GetDataAsNormalizedJsonString());
    }
    response->ReplyJson(chromeos::http::status_code::Ok,
                        chromeos::http::FormFieldList{});
  };

  transport_->AddHandler(command_url,
                         chromeos::http::request_type::kPatch,
                         base::Bind(update_command_progress));

  native_types::Object progress{
      {"progress", unittests::make_int_prop_value(18)}};
  command->SetProgress(progress);

  // UpdateCommand when changing command status.
  auto update_command_state = [](const ServerRequest& request,
                                 ServerResponse* response) {
    EXPECT_EQ(R"({"state":"cancelled"})",
              request.GetDataAsNormalizedJsonString());
    response->ReplyJson(chromeos::http::status_code::Ok,
                        chromeos::http::FormFieldList{});
  };

  transport_->AddHandler(command_url,
                         chromeos::http::request_type::kPatch,
                         base::Bind(update_command_state));

  command->Cancel();
}


}  // namespace buffet
