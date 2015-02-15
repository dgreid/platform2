// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "buffet/device_registration_info.h"

#include <base/json/json_reader.h>
#include <base/values.h>
#include <chromeos/bind_lambda.h>
#include <chromeos/http/http_request.h>
#include <chromeos/http/http_transport_fake.h>
#include <chromeos/key_value_store.h>
#include <chromeos/mime_utils.h>
#include <gtest/gtest.h>

#include "buffet/commands/command_manager.h"
#include "buffet/commands/unittest_utils.h"
#include "buffet/device_registration_storage_keys.h"
#include "buffet/states/mock_state_change_queue_interface.h"
#include "buffet/states/state_manager.h"
#include "buffet/storage_impls.h"

using chromeos::http::request_header::kAuthorization;
using chromeos::http::fake::ServerRequest;
using chromeos::http::fake::ServerResponse;

namespace buffet {

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

// Fill in the storage with default environment information (URLs, etc).
void InitDefaultStorage(base::DictionaryValue* data) {
  data->SetString(storage_keys::kClientId, test_data::kClientId);
  data->SetString(storage_keys::kClientSecret, test_data::kClientSecret);
  data->SetString(storage_keys::kApiKey, test_data::kApiKey);
  data->SetString(storage_keys::kRefreshToken, "");
  data->SetString(storage_keys::kDeviceId, "");
  data->SetString(storage_keys::kOAuthURL, test_data::kOAuthURL);
  data->SetString(storage_keys::kServiceURL, test_data::kServiceURL);
  data->SetString(storage_keys::kRobotAccount, "");
  data->SetString(storage_keys::kDeviceKind, "");
  data->SetString(storage_keys::kName, "");
  data->SetString(storage_keys::kDisplayName, "");
  data->SetString(storage_keys::kDescription, "");
  data->SetString(storage_keys::kLocation, "");
  data->SetString(storage_keys::kModelId, "");
}

// Add the test device registration information.
void SetDefaultDeviceRegistration(base::DictionaryValue* data) {
  data->SetString(storage_keys::kRefreshToken, test_data::kRefreshToken);
  data->SetString(storage_keys::kDeviceId, test_data::kDeviceId);
  data->SetString(storage_keys::kRobotAccount, test_data::kRobotAccountEmail);
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

// This is a helper class that allows the unit tests to access private
// methods and data since TestHelper is declared as a friend to
// DeviceRegistrationInfo.
class DeviceRegistrationInfo::TestHelper {
 public:
  static bool Save(DeviceRegistrationInfo* info) {
    return info->Save();
  }
};

class DeviceRegistrationInfoTest : public ::testing::Test {
 protected:
  void SetUp() override {
    InitDefaultStorage(&data_);
    storage_ = std::make_shared<MemStorage>();
    storage_->Save(&data_);
    transport_ = std::make_shared<chromeos::http::fake::Transport>();
    command_manager_ = std::make_shared<CommandManager>();
    state_manager_ = std::make_shared<StateManager>(&mock_state_change_queue_);
    std::unique_ptr<chromeos::KeyValueStore> config_store{
      new chromeos::KeyValueStore};
    config_store->SetString("client_id", test_data::kClientId);
    config_store->SetString("client_secret", test_data::kClientSecret);
    config_store->SetString("api_key", test_data::kApiKey);
    config_store->SetString("device_kind",  "vendor");
    config_store->SetString("name",  "coffee_pot");
    config_store->SetString("display_name",  "Coffee Pot");
    config_store->SetString("description",  "Easy to clean");
    config_store->SetString("location",  "Kitchen");
    config_store->SetString("model_id", "AAA");
    config_store->SetString("oauth_url", test_data::kOAuthURL);
    config_store->SetString("service_url", test_data::kServiceURL);
    auto mock_callback = base::Bind(
        &DeviceRegistrationInfoTest::OnRegistrationStatusChange,
        base::Unretained(this));
    dev_reg_ = std::unique_ptr<DeviceRegistrationInfo>(
        new DeviceRegistrationInfo(command_manager_, state_manager_,
                                   std::move(config_store),
                                   transport_, storage_,
                                   mock_callback));
  }

  MOCK_METHOD1(OnRegistrationStatusChange, void(RegistrationStatus));

  base::DictionaryValue data_;
  std::shared_ptr<MemStorage> storage_;
  std::shared_ptr<chromeos::http::fake::Transport> transport_;
  std::unique_ptr<DeviceRegistrationInfo> dev_reg_;
  std::shared_ptr<CommandManager> command_manager_;
  testing::NiceMock<MockStateChangeQueueInterface> mock_state_change_queue_;
  std::shared_ptr<StateManager> state_manager_;
};

////////////////////////////////////////////////////////////////////////////////
TEST_F(DeviceRegistrationInfoTest, GetServiceURL) {
  EXPECT_TRUE(dev_reg_->Load());
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

TEST_F(DeviceRegistrationInfoTest, VerifySave) {
  base::DictionaryValue data;
  data.SetString(storage_keys::kClientId, "a");
  data.SetString(storage_keys::kClientSecret, "b");
  data.SetString(storage_keys::kApiKey, "c");
  data.SetString(storage_keys::kRefreshToken, "d");
  data.SetString(storage_keys::kDeviceId, "e");
  data.SetString(storage_keys::kOAuthURL, "f");
  data.SetString(storage_keys::kServiceURL, "g");
  data.SetString(storage_keys::kRobotAccount, "h");
  data.SetString(storage_keys::kDeviceKind, "i");
  data.SetString(storage_keys::kName, "j");
  data.SetString(storage_keys::kDisplayName, "k");
  data.SetString(storage_keys::kDescription, "l");
  data.SetString(storage_keys::kLocation, "m");
  data.SetString(storage_keys::kModelId, "n");

  storage_->Save(&data);

  // This test isn't really trying to test Load, it is just the easiest
  // way to initialize the properties in dev_reg_.
  EXPECT_TRUE(dev_reg_->Load());

  // Clear the storage to get a clean test.
  base::DictionaryValue empty;
  storage_->Save(&empty);
  EXPECT_TRUE(DeviceRegistrationInfo::TestHelper::Save(dev_reg_.get()));
  EXPECT_TRUE(storage_->Load()->Equals(&data));
}

TEST_F(DeviceRegistrationInfoTest, GetOAuthURL) {
  EXPECT_TRUE(dev_reg_->Load());
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
  EXPECT_TRUE(dev_reg_->Load());
  EXPECT_FALSE(dev_reg_->CheckRegistration(nullptr));
  EXPECT_EQ(0, transport_->GetRequestCount());

  SetDefaultDeviceRegistration(&data_);
  storage_->Save(&data_);
  EXPECT_TRUE(dev_reg_->Load());

  transport_->AddHandler(dev_reg_->GetOAuthURL("token"),
                         chromeos::http::request_type::kPost,
                         base::Bind(OAuth2Handler));
  transport_->ResetRequestCount();
  EXPECT_TRUE(dev_reg_->CheckRegistration(nullptr));
  EXPECT_EQ(1, transport_->GetRequestCount());
}

TEST_F(DeviceRegistrationInfoTest, GetDeviceInfo) {
  SetDefaultDeviceRegistration(&data_);
  storage_->Save(&data_);
  EXPECT_TRUE(dev_reg_->Load());

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
  storage_->Save(&data_);
  EXPECT_TRUE(dev_reg_->Load());

  transport_->AddHandler(dev_reg_->GetOAuthURL("token"),
                         chromeos::http::request_type::kPost,
                         base::Bind(OAuth2Handler));
  transport_->AddHandler(dev_reg_->GetDeviceURL(),
                         chromeos::http::request_type::kGet,
                         base::Bind(DeviceInfoHandler));
  std::string id = dev_reg_->GetDeviceId(nullptr);
  EXPECT_EQ(test_data::kDeviceId, id);
}

TEST_F(DeviceRegistrationInfoTest, RegisterDevice) {
  EXPECT_TRUE(dev_reg_->Load());

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
    EXPECT_EQ("AAA", value);
    EXPECT_TRUE(json->GetString("deviceDraft.displayName", &value));
    EXPECT_EQ("Coffee Pot", value);
    base::DictionaryValue* commandDefs = nullptr;
    EXPECT_TRUE(json->GetDictionary("deviceDraft.commandDefs", &commandDefs));
    EXPECT_FALSE(commandDefs->empty());
    EXPECT_EQ("{'base':{'reboot':{'parameters':{"
              "'delay':{'minimum':10,'type':'integer'}}}},"
              "'robot':{'_jump':{'parameters':{"
              "'_height':{'type':'integer'}}}}}",
              buffet::unittests::ValueToString(commandDefs));

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

  auto json_base = buffet::unittests::CreateDictionaryValue(R"({
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
  auto json_cmds = buffet::unittests::CreateDictionaryValue(R"({
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
  storage_->reset_save_count();

  std::map<std::string, std::string> params;
  params["ticket_id"] = test_data::kClaimTicketId;
  std::string device_id = dev_reg_->RegisterDevice(params, nullptr);

  EXPECT_EQ(test_data::kDeviceId, device_id);
  EXPECT_EQ(1,
            storage_->save_count());  // The device info must have been saved.
  EXPECT_EQ(3, transport_->GetRequestCount());
  EXPECT_EQ(RegistrationStatus::kRegistered, dev_reg_->GetRegistrationStatus());

  // Validate the device info saved to storage...
  auto storage_data = storage_->Load();
  base::DictionaryValue* dict = nullptr;
  EXPECT_TRUE(storage_data->GetAsDictionary(&dict));
  std::string value;
  EXPECT_TRUE(dict->GetString(storage_keys::kApiKey, &value));
  EXPECT_EQ(test_data::kApiKey, value);
  EXPECT_TRUE(dict->GetString(storage_keys::kClientId, &value));
  EXPECT_EQ(test_data::kClientId, value);
  EXPECT_TRUE(dict->GetString(storage_keys::kClientSecret, &value));
  EXPECT_EQ(test_data::kClientSecret, value);
  EXPECT_TRUE(dict->GetString(storage_keys::kDeviceId, &value));
  EXPECT_EQ(test_data::kDeviceId, value);
  EXPECT_TRUE(dict->GetString(storage_keys::kOAuthURL, &value));
  EXPECT_EQ(test_data::kOAuthURL, value);
  EXPECT_TRUE(dict->GetString(storage_keys::kRefreshToken, &value));
  EXPECT_EQ(test_data::kRefreshToken, value);
  EXPECT_TRUE(dict->GetString(storage_keys::kRobotAccount, &value));
  EXPECT_EQ(test_data::kRobotAccountEmail, value);
  EXPECT_TRUE(dict->GetString(storage_keys::kServiceURL, &value));
  EXPECT_EQ(test_data::kServiceURL, value);
}

TEST_F(DeviceRegistrationInfoTest, OOBRegistrationStatus) {
  // After we've been initialized, we should be either offline or unregistered,
  // depending on whether or not we've found credentials.
  EXPECT_TRUE(dev_reg_->Load());
  EXPECT_EQ(RegistrationStatus::kUnregistered,
            dev_reg_->GetRegistrationStatus());
  // Put some credentials into our state, make sure we call that offline.
  SetDefaultDeviceRegistration(&data_);
  storage_->Save(&data_);
  EXPECT_TRUE(dev_reg_->Load());
  EXPECT_EQ(RegistrationStatus::kOffline, dev_reg_->GetRegistrationStatus());
}

}  // namespace buffet
