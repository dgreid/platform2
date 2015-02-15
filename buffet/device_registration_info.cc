// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "buffet/device_registration_info.h"

#include <memory>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/json/json_writer.h>
#include <base/message_loop/message_loop.h>
#include <base/values.h>
#include <chromeos/bind_lambda.h>
#include <chromeos/data_encoding.h>
#include <chromeos/errors/error_codes.h>
#include <chromeos/http/http_utils.h>
#include <chromeos/key_value_store.h>
#include <chromeos/mime_utils.h>
#include <chromeos/strings/string_utils.h>
#include <chromeos/url_utils.h>

#include "buffet/commands/cloud_command_proxy.h"
#include "buffet/commands/command_definition.h"
#include "buffet/commands/command_manager.h"
#include "buffet/device_registration_storage_keys.h"
#include "buffet/states/state_manager.h"
#include "buffet/utils.h"

const char buffet::kErrorDomainOAuth2[] = "oauth2";
const char buffet::kErrorDomainGCD[] = "gcd";
const char buffet::kErrorDomainGCDServer[] = "gcd_server";

namespace buffet {
namespace storage_keys {

// Persistent keys
const char kClientId[]      = "client_id";
const char kClientSecret[]  = "client_secret";
const char kApiKey[]        = "api_key";
const char kRefreshToken[]  = "refresh_token";
const char kDeviceId[]      = "device_id";
const char kOAuthURL[]      = "oauth_url";
const char kServiceURL[]    = "service_url";
const char kRobotAccount[]  = "robot_account";
// Transient keys
const char kDeviceKind[]    = "device_kind";
const char kName[]          = "name";
const char kDisplayName[]   = "display_name";
const char kDescription[]   = "description";
const char kLocation[]      = "location";
const char kModelId[]       = "model_id";


}  // namespace storage_keys
}  // namespace buffet

namespace {

std::pair<std::string, std::string> BuildAuthHeader(
    const std::string& access_token_type,
    const std::string& access_token) {
  std::string authorization =
      chromeos::string_utils::Join(' ', access_token_type, access_token);
  return {chromeos::http::request_header::kAuthorization, authorization};
}

std::unique_ptr<base::DictionaryValue> ParseOAuthResponse(
    const chromeos::http::Response* response, chromeos::ErrorPtr* error) {
  int code = 0;
  auto resp = chromeos::http::ParseJsonResponse(response, &code, error);
  if (resp && code >= chromeos::http::status_code::BadRequest) {
    if (error) {
      std::string error_code, error_message;
      if (resp->GetString("error", &error_code) &&
          resp->GetString("error_description", &error_message)) {
        chromeos::Error::AddTo(error, FROM_HERE, buffet::kErrorDomainOAuth2,
                               error_code, error_message);
      } else {
        chromeos::Error::AddTo(error, FROM_HERE, buffet::kErrorDomainOAuth2,
                               "unexpected_response", "Unexpected OAuth error");
      }
    }
    return std::unique_ptr<base::DictionaryValue>();
  }
  return resp;
}

inline void SetUnexpectedError(chromeos::ErrorPtr* error) {
  chromeos::Error::AddTo(error, FROM_HERE, buffet::kErrorDomainGCD,
                         "unexpected_response", "Unexpected GCD error");
}

void ParseGCDError(const base::DictionaryValue* json,
                   chromeos::ErrorPtr* error) {
  if (!error)
    return;

  const base::Value* list_value = nullptr;
  const base::ListValue* error_list = nullptr;
  if (!json->Get("error.errors", &list_value) ||
      !list_value->GetAsList(&error_list)) {
    SetUnexpectedError(error);
    return;
  }

  for (size_t i = 0; i < error_list->GetSize(); i++) {
    const base::Value* error_value = nullptr;
    const base::DictionaryValue* error_object = nullptr;
    if (!error_list->Get(i, &error_value) ||
        !error_value->GetAsDictionary(&error_object)) {
      SetUnexpectedError(error);
      continue;
    }
    std::string error_code, error_message;
    if (error_object->GetString("reason", &error_code) &&
        error_object->GetString("message", &error_message)) {
      chromeos::Error::AddTo(error, FROM_HERE, buffet::kErrorDomainGCDServer,
                             error_code, error_message);
    } else {
      SetUnexpectedError(error);
    }
  }
}

std::string BuildURL(const std::string& url,
                     const std::vector<std::string>& subpaths,
                     const chromeos::data_encoding::WebParamList& params) {
  std::string result = chromeos::url::CombineMultiple(url, subpaths);
  return chromeos::url::AppendQueryParams(result, params);
}

void IgnoreCloudError(const chromeos::Error*) {
}

void IgnoreCloudResult(const base::DictionaryValue&) {
}

}  // anonymous namespace

namespace buffet {

DeviceRegistrationInfo::DeviceRegistrationInfo(
    const std::shared_ptr<CommandManager>& command_manager,
    const std::shared_ptr<StateManager>& state_manager,
    std::unique_ptr<chromeos::KeyValueStore> config_store,
    const std::shared_ptr<chromeos::http::Transport>& transport,
    const std::shared_ptr<StorageInterface>& state_store,
    const StatusHandler& status_handler)
    : transport_{transport},
      storage_{state_store},
      command_manager_{command_manager},
      state_manager_{state_manager},
      config_store_{std::move(config_store)},
      registration_status_handler_{status_handler} {
}

DeviceRegistrationInfo::~DeviceRegistrationInfo() = default;

std::pair<std::string, std::string>
    DeviceRegistrationInfo::GetAuthorizationHeader() const {
  return BuildAuthHeader("Bearer", access_token_);
}

std::string DeviceRegistrationInfo::GetServiceURL(
    const std::string& subpath,
    const chromeos::data_encoding::WebParamList& params) const {
  return BuildURL(service_url_, {subpath}, params);
}

std::string DeviceRegistrationInfo::GetDeviceURL(
    const std::string& subpath,
    const chromeos::data_encoding::WebParamList& params) const {
  CHECK(!device_id_.empty()) << "Must have a valid device ID";
  return BuildURL(service_url_, {"devices", device_id_, subpath}, params);
}

std::string DeviceRegistrationInfo::GetOAuthURL(
    const std::string& subpath,
    const chromeos::data_encoding::WebParamList& params) const {
  return BuildURL(oauth_url_, {subpath}, params);
}

std::string DeviceRegistrationInfo::GetDeviceId(chromeos::ErrorPtr* error) {
  return CheckRegistration(error) ? device_id_ : std::string();
}

bool DeviceRegistrationInfo::Load() {
  auto value = storage_->Load();
  const base::DictionaryValue* dict = nullptr;
  if (!value || !value->GetAsDictionary(&dict))
    return false;

  // Get the values into temp variables first to make sure we can get
  // all the data correctly before changing the state of this object.
  std::string client_id;
  if (!dict->GetString(storage_keys::kClientId, &client_id))
    return false;
  std::string client_secret;
  if (!dict->GetString(storage_keys::kClientSecret, &client_secret))
    return false;
  std::string api_key;
  if (!dict->GetString(storage_keys::kApiKey, &api_key))
    return false;
  std::string refresh_token;
  if (!dict->GetString(storage_keys::kRefreshToken, &refresh_token))
    return false;
  std::string device_id;
  if (!dict->GetString(storage_keys::kDeviceId, &device_id))
    return false;
  std::string oauth_url;
  if (!dict->GetString(storage_keys::kOAuthURL, &oauth_url))
    return false;
  std::string service_url;
  if (!dict->GetString(storage_keys::kServiceURL, &service_url))
    return false;
  std::string device_robot_account;
  if (!dict->GetString(storage_keys::kRobotAccount, &device_robot_account))
    return false;
  std::string device_kind;
  if (!dict->GetString(storage_keys::kDeviceKind, &device_kind))
    return false;
  std::string name;
  if (!dict->GetString(storage_keys::kName, &name))
    return false;
  std::string display_name;
  if (!dict->GetString(storage_keys::kDisplayName, &display_name))
    return false;
  std::string description;
  if (!dict->GetString(storage_keys::kDescription, &description))
    return false;
  std::string location;
  if (!dict->GetString(storage_keys::kLocation, &location))
    return false;

  // Temporarily tolerate missing modelId. Older registrations will not have a
  // modelId in their state files.
  // TODO(vitalybuka): Add result check back. Should be safe starting Mar 2015.
  dict->GetString(storage_keys::kModelId, &model_id_);

  client_id_            = client_id;
  client_secret_        = client_secret;
  api_key_              = api_key;
  refresh_token_        = refresh_token;
  device_id_            = device_id;
  oauth_url_            = oauth_url;
  service_url_          = service_url;
  device_robot_account_ = device_robot_account;
  device_kind_          = device_kind;
  name_                 = name;
  display_name_         = display_name;
  description_          = description;
  location_             = location;

  if (HaveRegistrationCredentials(nullptr))
    SetRegistrationStatus(RegistrationStatus::kOffline);

  return true;
}

bool DeviceRegistrationInfo::Save() const {
  base::DictionaryValue dict;
  dict.SetString(storage_keys::kClientId,     client_id_);
  dict.SetString(storage_keys::kClientSecret, client_secret_);
  dict.SetString(storage_keys::kApiKey,       api_key_);
  dict.SetString(storage_keys::kRefreshToken, refresh_token_);
  dict.SetString(storage_keys::kDeviceId,     device_id_);
  dict.SetString(storage_keys::kOAuthURL,     oauth_url_);
  dict.SetString(storage_keys::kServiceURL,   service_url_);
  dict.SetString(storage_keys::kRobotAccount, device_robot_account_);
  dict.SetString(storage_keys::kDeviceKind,   device_kind_);
  dict.SetString(storage_keys::kName,         name_);
  dict.SetString(storage_keys::kDisplayName,  display_name_);
  dict.SetString(storage_keys::kDescription,  description_);
  dict.SetString(storage_keys::kLocation,     location_);
  dict.SetString(storage_keys::kModelId,      model_id_);

  return storage_->Save(&dict);
}

void DeviceRegistrationInfo::ScheduleStartDevice(const base::TimeDelta& later) {
  base::MessageLoop* current = base::MessageLoop::current();
  if (!current)
    return;  // Assume we're in unittests
  current->PostDelayedTask(
      FROM_HERE,
      base::Bind(&DeviceRegistrationInfo::StartDevice,
                 weak_factory_.GetWeakPtr(), nullptr),
      later);
}

bool DeviceRegistrationInfo::CheckRegistration(chromeos::ErrorPtr* error) {
  return HaveRegistrationCredentials(error) &&
         ValidateAndRefreshAccessToken(error);
}

bool DeviceRegistrationInfo::HaveRegistrationCredentials(
    chromeos::ErrorPtr* error) {
  const bool have_credentials = !refresh_token_.empty() &&
                                !device_id_.empty() &&
                                !device_robot_account_.empty();

  VLOG(1) << "Device registration record "
          << ((have_credentials) ? "found" : "not found.");
  if (!have_credentials)
    chromeos::Error::AddTo(error, FROM_HERE, kErrorDomainGCD,
                           "device_not_registered",
                           "No valid device registration record found");
  return have_credentials;
}

bool DeviceRegistrationInfo::ValidateAndRefreshAccessToken(
    chromeos::ErrorPtr* error) {
  LOG(INFO) << "Checking access token expiration.";
  if (!access_token_.empty() &&
      !access_token_expiration_.is_null() &&
      access_token_expiration_ > base::Time::Now()) {
    LOG(INFO) << "Access token is still valid.";
    return true;
  }

  auto response = chromeos::http::PostFormDataAndBlock(GetOAuthURL("token"), {
    {"refresh_token", refresh_token_},
    {"client_id", client_id_},
    {"client_secret", client_secret_},
    {"grant_type", "refresh_token"},
  }, {}, transport_, error);
  if (!response)
    return false;

  auto json = ParseOAuthResponse(response.get(), error);
  if (!json)
    return false;

  int expires_in = 0;
  if (!json->GetString("access_token", &access_token_) ||
      !json->GetInteger("expires_in", &expires_in) ||
      access_token_.empty() ||
      expires_in <= 0) {
    LOG(ERROR) << "Access token unavailable.";
    chromeos::Error::AddTo(error, FROM_HERE, kErrorDomainOAuth2,
                           "unexpected_server_response",
                           "Access token unavailable");
    return false;
  }
  access_token_expiration_ = base::Time::Now() +
                             base::TimeDelta::FromSeconds(expires_in);
  LOG(INFO) << "Access token is refreshed for additional " << expires_in
            << " seconds.";

  StartXmpp();

  return true;
}

void DeviceRegistrationInfo::StartXmpp() {
  // If no MessageLoop assume we're in unittests.
  if (!base::MessageLoop::current()) {
    LOG(INFO) << "No MessageLoop, not starting XMPP";
    return;
  }

  if (!fd_watcher_.StopWatchingFileDescriptor()) {
    LOG(WARNING) << "Failed to stop the previous watcher";
    return;
  }

  std::unique_ptr<XmppConnection> connection(new XmppConnection());
  if (!connection->Initialize()) {
    LOG(WARNING) << "Failed to connect to XMPP server";
    return;
  }
  xmpp_client_.reset(new XmppClient(device_robot_account_, access_token_,
                                    std::move(connection)));
  if (!base::MessageLoopForIO::current()->WatchFileDescriptor(
          xmpp_client_->GetFileDescriptor(), true /* persistent */,
          base::MessageLoopForIO::WATCH_READ, &fd_watcher_, this)) {
    LOG(WARNING) << "Failed to watch XMPP file descriptor";
    return;
  }
  xmpp_client_->StartStream();
}

void DeviceRegistrationInfo::OnFileCanReadWithoutBlocking(int fd) {
  if (xmpp_client_ && xmpp_client_->GetFileDescriptor() == fd) {
    if (!xmpp_client_->Read()) {
      // Authentication failed or the socket was closed.
      if (!fd_watcher_.StopWatchingFileDescriptor()) {
        LOG(WARNING) << "Failed to stop the watcher";
      }
    }
  }
}

std::unique_ptr<base::DictionaryValue>
DeviceRegistrationInfo::BuildDeviceResource(chromeos::ErrorPtr* error) {
  std::unique_ptr<base::DictionaryValue> commands =
      command_manager_->GetCommandDictionary().GetCommandsAsJson(true, error);
  if (!commands)
    return nullptr;

  std::unique_ptr<base::DictionaryValue> state =
      state_manager_->GetStateValuesAsJson(error);
  if (!state)
    return nullptr;

  std::unique_ptr<base::DictionaryValue> resource{new base::DictionaryValue};
  if (!device_id_.empty())
    resource->SetString("id", device_id_);
  resource->SetString("deviceKind", device_kind_);
  resource->SetString("name", name_);
  if (!display_name_.empty())
    resource->SetString("displayName", display_name_);
  if (!description_.empty())
    resource->SetString("description", description_);
  if (!location_.empty())
    resource->SetString("location", location_);
  if (!model_id_.empty())
    resource->SetString("modelManifestId", model_id_);
  resource->SetString("channel.supportedType", "xmpp");
  resource->Set("commandDefs", commands.release());
  resource->Set("state", state.release());

  return resource;
}

std::unique_ptr<base::Value> DeviceRegistrationInfo::GetDeviceInfo(
    chromeos::ErrorPtr* error) {
  if (!CheckRegistration(error))
    return std::unique_ptr<base::Value>();

  // TODO(antonm): Switch to DoCloudRequest later.
  auto response = chromeos::http::GetAndBlock(
      GetDeviceURL(), {GetAuthorizationHeader()}, transport_, error);
  int status_code = 0;
  std::unique_ptr<base::DictionaryValue> json =
      chromeos::http::ParseJsonResponse(response.get(), &status_code, error);
  if (json) {
    if (status_code >= chromeos::http::status_code::BadRequest) {
      LOG(WARNING) << "Failed to retrieve the device info. Response code = "
                   << status_code;
      ParseGCDError(json.get(), error);
      return std::unique_ptr<base::Value>();
    }
  }
  return std::unique_ptr<base::Value>(json.release());
}

std::string DeviceRegistrationInfo::RegisterDevice(
    const std::map<std::string, std::string>& params,
    chromeos::ErrorPtr* error) {
  std::string ticket_id;
  if (!GetParamValue(params, "ticket_id", &ticket_id, error) ||
      !GetParamValue(params, storage_keys::kClientId, &client_id_, error) ||
      !GetParamValue(params, storage_keys::kClientSecret, &client_secret_,
                     error) ||
      !GetParamValue(params, storage_keys::kApiKey, &api_key_, error) ||
      !GetParamValue(params, storage_keys::kDeviceKind, &device_kind_, error) ||
      !GetParamValue(params, storage_keys::kName, &name_, error) ||
      !GetParamValue(params, storage_keys::kDisplayName, &display_name_,
                     error) ||
      !GetParamValue(params, storage_keys::kDescription, &description_,
                     error) ||
      !GetParamValue(params, storage_keys::kLocation, &location_, error) ||
      !GetParamValue(params, storage_keys::kModelId, &model_id_, error) ||
      !GetParamValue(params, storage_keys::kOAuthURL, &oauth_url_, error) ||
      !GetParamValue(params, storage_keys::kServiceURL, &service_url_, error)) {
    return std::string();
  }

  std::unique_ptr<base::DictionaryValue> device_draft =
      BuildDeviceResource(error);
  if (!device_draft)
    return std::string();

  base::DictionaryValue req_json;
  req_json.SetString("id", ticket_id);
  req_json.SetString("oauthClientId", client_id_);
  req_json.Set("deviceDraft", device_draft.release());

  auto url = GetServiceURL("registrationTickets/" + ticket_id,
                           {{"key", api_key_}});
  std::unique_ptr<chromeos::http::Response> response =
      chromeos::http::PatchJsonAndBlock(url, &req_json, {}, transport_, error);
  auto json_resp = chromeos::http::ParseJsonResponse(response.get(), nullptr,
                                                     error);
  if (!json_resp)
    return std::string();
  if (!response->IsSuccessful()) {
    ParseGCDError(json_resp.get(), error);
    return std::string();
  }

  url = GetServiceURL("registrationTickets/" + ticket_id +
                      "/finalize?key=" + api_key_);
  response = chromeos::http::SendRequestWithNoDataAndBlock(
      chromeos::http::request_type::kPost, url, {}, transport_, error);
  if (!response)
    return std::string();
  json_resp = chromeos::http::ParseJsonResponse(response.get(), nullptr, error);
  if (!json_resp)
    return std::string();
  if (!response->IsSuccessful()) {
    ParseGCDError(json_resp.get(), error);
    return std::string();
  }

  std::string auth_code;
  if (!json_resp->GetString("robotAccountEmail", &device_robot_account_) ||
      !json_resp->GetString("robotAccountAuthorizationCode", &auth_code) ||
      !json_resp->GetString("deviceDraft.id", &device_id_)) {
    chromeos::Error::AddTo(error, FROM_HERE, kErrorDomainGCD,
                           "unexpected_response",
                           "Device account missing in response");
    return std::string();
  }

  // Now get access_token and refresh_token
  response = chromeos::http::PostFormDataAndBlock(GetOAuthURL("token"), {
    {"code", auth_code},
    {"client_id", client_id_},
    {"client_secret", client_secret_},
    {"redirect_uri", "oob"},
    {"scope", "https://www.googleapis.com/auth/clouddevices"},
    {"grant_type", "authorization_code"}
  }, {}, transport_, error);
  if (!response)
    return std::string();

  json_resp = ParseOAuthResponse(response.get(), error);
  int expires_in = 0;
  if (!json_resp ||
      !json_resp->GetString("access_token", &access_token_) ||
      !json_resp->GetString("refresh_token", &refresh_token_) ||
      !json_resp->GetInteger("expires_in", &expires_in) ||
      access_token_.empty() ||
      refresh_token_.empty() ||
      expires_in <= 0) {
    chromeos::Error::AddTo(error, FROM_HERE,
                           kErrorDomainGCD, "unexpected_response",
                           "Device access_token missing in response");
    return std::string();
  }

  access_token_expiration_ = base::Time::Now() +
                             base::TimeDelta::FromSeconds(expires_in);

  Save();
  StartXmpp();

  // We're going to respond with our success immediately and we'll StartDevice
  // shortly after.
  ScheduleStartDevice(base::TimeDelta::FromSeconds(0));
  SetRegistrationStatus(RegistrationStatus::kRegistered);
  return device_id_;
}

namespace {

template <class T>
void PostToCallback(base::Callback<void(const T&)> callback,
                    std::unique_ptr<T> value) {
  auto cb = [callback] (T* result) {
    callback.Run(*result);
  };
  base::MessageLoop::current()->PostTask(
      FROM_HERE, base::Bind(cb, base::Owned(value.release())));
}

void PostRepeatingTask(const tracked_objects::Location& from_here,
                       base::Closure task,
                       base::TimeDelta delay) {
  task.Run();
  base::MessageLoop::current()->PostDelayedTask(
      from_here, base::Bind(&PostRepeatingTask, from_here, task, delay), delay);
}

using ResponsePtr = scoped_ptr<chromeos::http::Response>;

void SendRequestWithRetries(
    const std::string& method,
    const std::string& url,
    const std::string& data,
    const std::string& mime_type,
    const chromeos::http::HeaderList& headers,
    std::shared_ptr<chromeos::http::Transport> transport,
    int num_retries,
    const chromeos::http::SuccessCallback& success_callback,
    const chromeos::http::ErrorCallback& error_callback) {
  auto on_failure =
      [method, url, data, mime_type, headers, transport, num_retries,
      success_callback, error_callback](int request_id,
                                        const chromeos::Error* error) {
    if (num_retries > 0) {
      SendRequestWithRetries(method, url, data, mime_type,
                             headers, transport, num_retries - 1,
                             success_callback, error_callback);
    } else {
      error_callback.Run(request_id, error);
    }
  };

  auto on_success =
      [on_failure, success_callback, error_callback](int request_id,
                                                     ResponsePtr response) {
    int status_code = response->GetStatusCode();
    if (status_code >= chromeos::http::status_code::Continue &&
        status_code < chromeos::http::status_code::BadRequest) {
      success_callback.Run(request_id, response.Pass());
      return;
    }

    // TODO(antonm): Should add some useful information to error.
    LOG(WARNING) << "Request failed. Response code = " << status_code;

    chromeos::ErrorPtr error;
    chromeos::Error::AddTo(&error, FROM_HERE, chromeos::errors::http::kDomain,
                           std::to_string(status_code),
                           response->GetStatusText());
    if (status_code >= chromeos::http::status_code::InternalServerError &&
        status_code < 600) {
      // Request was valid, but server failed, retry.
      // TODO(antonm): Implement exponential backoff.
      // TODO(antonm): Reconsider status codes, maybe only some require
      // retry.
      // TODO(antonm): Support Retry-After header.
      on_failure(request_id, error.get());
    } else {
      error_callback.Run(request_id, error.get());
    }
  };

  chromeos::http::SendRequest(method, url, data.c_str(), data.size(),
                              mime_type, headers, transport,
                              base::Bind(on_success),
                              base::Bind(on_failure));
}

}  // namespace

void DeviceRegistrationInfo::DoCloudRequest(
    const std::string& method,
    const std::string& url,
    const base::DictionaryValue* body,
    const CloudRequestCallback& success_callback,
    const CloudRequestErrorCallback& error_callback) {
  // TODO(antonm): Add reauthorization on access token expiration (do not
  // forget about 5xx when fetching new access token).
  // TODO(antonm): Add support for device removal.

  std::string data;
  if (body)
    base::JSONWriter::Write(body, &data);

  const std::string mime_type{chromeos::mime::AppendParameter(
      chromeos::mime::application::kJson,
      chromeos::mime::parameters::kCharset,
      "utf-8")};

  auto request_cb =
      [success_callback, error_callback](int request_id, ResponsePtr response) {
    chromeos::ErrorPtr error;

    std::unique_ptr<base::DictionaryValue> json_resp{
        chromeos::http::ParseJsonResponse(response.get(), nullptr, &error)};
    if (!json_resp) {
      error_callback.Run(error.get());
      return;
    }

    success_callback.Run(*json_resp);
  };

  auto error_cb =
      [error_callback](int request_id, const chromeos::Error* error) {
    error_callback.Run(error);
  };

  auto transport = transport_;
  auto error_callackback_with_reauthorization =
      base::Bind([method, url, data, mime_type, transport, request_cb, error_cb]
          (DeviceRegistrationInfo* self,
          int request_id,
          const chromeos::Error* error) {
    if (error->HasError(chromeos::errors::http::kDomain,
                        std::to_string(chromeos::http::status_code::Denied))) {
      chromeos::ErrorPtr reauthorization_error;
      if (!self->ValidateAndRefreshAccessToken(&reauthorization_error)) {
        // TODO(antonm): Check if the device has been actually removed.
        error_cb(request_id, reauthorization_error.get());
        return;
      }
      SendRequestWithRetries(method, url,
                             data, mime_type,
                             {self->GetAuthorizationHeader()},
                             transport,
                             7,
                             base::Bind(request_cb), base::Bind(error_cb));
    } else {
      error_cb(request_id, error);
    }
  }, base::Unretained(this));

  SendRequestWithRetries(method, url,
                         data, mime_type,
                         {GetAuthorizationHeader()},
                         transport,
                         7,
                         base::Bind(request_cb),
                         error_callackback_with_reauthorization);
}

void DeviceRegistrationInfo::StartDevice(chromeos::ErrorPtr* error) {
  if (!CheckRegistration(error))
    return;
  base::Bind(
      &DeviceRegistrationInfo::UpdateDeviceResource,
      base::Unretained(this),
      base::Bind(
          &DeviceRegistrationInfo::FetchCommands,
          base::Unretained(this),
          base::Bind(
              &DeviceRegistrationInfo::AbortLimboCommands,
              base::Unretained(this),
              base::Bind(
                  &DeviceRegistrationInfo::PeriodicallyPollCommands,
                  base::Unretained(this))))).Run();
}

void DeviceRegistrationInfo::UpdateCommand(
    const std::string& command_id,
    const base::DictionaryValue& command_patch) {
  DoCloudRequest(
      chromeos::http::request_type::kPatch,
      GetServiceURL("commands/" + command_id),
      &command_patch,
      base::Bind(&IgnoreCloudResult), base::Bind(&IgnoreCloudError));
}

void DeviceRegistrationInfo::UpdateDeviceResource(base::Closure callback) {
  std::unique_ptr<base::DictionaryValue> device_resource =
      BuildDeviceResource(nullptr);
  if (!device_resource)
    return;

  DoCloudRequest(
      chromeos::http::request_type::kPut,
      GetDeviceURL(),
      device_resource.get(),
      base::Bind([callback](const base::DictionaryValue&){
        base::MessageLoop::current()->PostTask(FROM_HERE, callback);
      }),
      // TODO(antonm): Failure to update device resource probably deserves
      // some additional actions.
      base::Bind(&IgnoreCloudError));
}

void DeviceRegistrationInfo::FetchCommands(
    base::Callback<void(const base::ListValue&)> callback) {
  DoCloudRequest(
      chromeos::http::request_type::kGet,
      GetServiceURL("commands/queue", {{"deviceId", device_id_}}),
      nullptr,
      base::Bind([callback](const base::DictionaryValue& json) {
        const base::ListValue* commands{nullptr};
        if (!json.GetList("commands", &commands)) {
          VLOG(1) << "No commands in the response.";
        }
        const base::ListValue empty;
        callback.Run(commands ? *commands : empty);
      }),
      base::Bind(&IgnoreCloudError));
}

void DeviceRegistrationInfo::AbortLimboCommands(
    base::Closure callback, const base::ListValue& commands) {
  const size_t size{commands.GetSize()};
  for (size_t i = 0; i < size; ++i) {
    const base::DictionaryValue* command{nullptr};
    if (!commands.GetDictionary(i, &command)) {
      LOG(WARNING) << "No command resource at " << i;
      continue;
    }
    std::string command_state;
    if (!command->GetString("state", &command_state)) {
      LOG(WARNING) << "Command with no state at " << i;
      continue;
    }
    if (command_state != "error" &&
        command_state != "inProgress" &&
        command_state != "paused") {
      // It's not a limbo command, ignore.
      continue;
    }
    std::string command_id;
    if (!command->GetString("id", &command_id)) {
      LOG(WARNING) << "Command with no ID at " << i;
      continue;
    }

    std::unique_ptr<base::DictionaryValue> command_copy{command->DeepCopy()};
    command_copy->SetString("state", "aborted");
    DoCloudRequest(
        chromeos::http::request_type::kPut,
        GetServiceURL("commands/" + command_id),
        command_copy.get(),
        base::Bind(&IgnoreCloudResult), base::Bind(&IgnoreCloudError));
  }

  base::MessageLoop::current()->PostTask(FROM_HERE, callback);
}

void DeviceRegistrationInfo::PeriodicallyPollCommands() {
  VLOG(1) << "Poll commands";
  PostRepeatingTask(
      FROM_HERE,
      base::Bind(
          &DeviceRegistrationInfo::FetchCommands,
          base::Unretained(this),
          base::Bind(&DeviceRegistrationInfo::PublishCommands,
                     base::Unretained(this))),
      base::TimeDelta::FromSeconds(7));
  // TODO(antonm): Use better trigger: when StateManager registers new updates,
  // it should call closure which will post a task, probably with some
  // throttling, to publish state updates.
  PostRepeatingTask(
      FROM_HERE,
      base::Bind(&DeviceRegistrationInfo::PublishStateUpdates,
                 base::Unretained(this)),
      base::TimeDelta::FromSeconds(7));
  // TODO(wiley): This is the very bare minimum of state to report to local
  //              services interested in our GCD state.  Build a more
  //              robust model of our state with respect to the server.
  SetRegistrationStatus(RegistrationStatus::kRegistered);
}

void DeviceRegistrationInfo::PublishCommands(const base::ListValue& commands) {
  const CommandDictionary& command_dictionary =
      command_manager_->GetCommandDictionary();

  const size_t size{commands.GetSize()};
  for (size_t i = 0; i < size; ++i) {
    const base::DictionaryValue* command{nullptr};
    if (!commands.GetDictionary(i, &command)) {
      LOG(WARNING) << "No command resource at " << i;
      continue;
    }

    std::unique_ptr<CommandInstance> command_instance =
        CommandInstance::FromJson(command, command_dictionary, nullptr);
    if (!command_instance) {
      LOG(WARNING) << "Failed to parse a command";
      continue;
    }

    // TODO(antonm): Properly process cancellation of commands.
    if (!command_manager_->FindCommand(command_instance->GetID())) {
      std::unique_ptr<CommandProxyInterface> cloud_proxy{
          new CloudCommandProxy(command_instance.get(), this)};
      command_instance->AddProxy(std::move(cloud_proxy));
      command_manager_->AddCommand(std::move(command_instance));
    }
  }
}

void DeviceRegistrationInfo::PublishStateUpdates() {
  VLOG(1) << "PublishStateUpdates";
  const std::vector<StateChange> state_changes{
      state_manager_->GetAndClearRecordedStateChanges()};
  if (state_changes.empty())
    return;

  std::unique_ptr<base::ListValue> patches{new base::ListValue};
  for (const auto& state_change : state_changes) {
    std::unique_ptr<base::DictionaryValue> patch{new base::DictionaryValue};
    patch->SetString("timeMs",
                     std::to_string(state_change.timestamp.ToJavaTime()));

    std::unique_ptr<base::DictionaryValue> changes{new base::DictionaryValue};
    for (const auto& pair : state_change.changed_properties) {
      auto value = pair.second->ToJson(nullptr);
      if (!value) {
        return;
      }
      // The key in |pair.first| is the full property name in format
      // "package.property_name", so must use DictionaryValue::Set() instead of
      // DictionaryValue::SetWithoutPathExpansion to recreate the JSON
      // property tree properly.
      changes->Set(pair.first, value.release());
    }
    patch->Set("patch", changes.release());

    patches->Append(patch.release());
  }

  base::DictionaryValue body;
  body.SetString("requestTimeMs",
                 std::to_string(base::Time::Now().ToJavaTime()));
  body.Set("patches", patches.release());

  DoCloudRequest(
      chromeos::http::request_type::kPost,
      GetDeviceURL("patchState"),
      &body,
      base::Bind(&IgnoreCloudResult), base::Bind(&IgnoreCloudError));
}

bool DeviceRegistrationInfo::GetParamValue(
    const std::map<std::string, std::string>& params,
    const std::string& param_name,
    std::string* param_value,
    chromeos::ErrorPtr* error) {
  auto p = params.find(param_name);
  if (p != params.end()) {
    *param_value = p->second;
    return true;
  }

  bool default_was_set = config_store_->GetString(param_name, param_value);
  if (default_was_set)
    return true;

  chromeos::Error::AddToPrintf(error, FROM_HERE, kErrorDomainBuffet,
                               "missing_parameter",
                               "Parameter %s not specified",
                               param_name.c_str());
  return false;
}

void DeviceRegistrationInfo::SetRegistrationStatus(
    RegistrationStatus new_status) {
  if (new_status == registration_status_)
    return;
  VLOG(1) << "Changing registration status to " << StatusToString(new_status);
  registration_status_ = new_status;
  if (!registration_status_handler_.is_null())
    registration_status_handler_.Run(registration_status_);
}

}  // namespace buffet
