// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "buffet/device_registration_info.h"

#include <memory>
#include <set>
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
#include "buffet/commands/schema_constants.h"
#include "buffet/notification/xmpp_channel.h"
#include "buffet/states/state_manager.h"
#include "buffet/utils.h"

const char buffet::kErrorDomainOAuth2[] = "oauth2";
const char buffet::kErrorDomainGCD[] = "gcd";
const char buffet::kErrorDomainGCDServer[] = "gcd_server";

namespace {

const int kMaxStartDeviceRetryDelayMinutes{1};
const int64_t kMinStartDeviceRetryDelaySeconds{5};
const int64_t kAbortCommandRetryDelaySeconds{5};

std::pair<std::string, std::string> BuildAuthHeader(
    const std::string& access_token_type,
    const std::string& access_token) {
  std::string authorization =
      chromeos::string_utils::Join(" ", access_token_type, access_token);
  return {chromeos::http::request_header::kAuthorization, authorization};
}

inline void SetUnexpectedError(chromeos::ErrorPtr* error) {
  chromeos::Error::AddTo(error, FROM_HERE, buffet::kErrorDomainGCD,
                         "unexpected_response", "Unexpected GCD error");
}

void ParseGCDError(const base::DictionaryValue* json,
                   chromeos::ErrorPtr* error) {
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

void IgnoreCloudErrorWithCallback(const base::Closure& cb,
                                  const chromeos::Error*) {
  cb.Run();
}

void IgnoreCloudResult(const base::DictionaryValue&) {
}

void IgnoreCloudResultWithCallback(const base::Closure& cb,
                                   const base::DictionaryValue&) {
  cb.Run();
}

}  // anonymous namespace

namespace buffet {

DeviceRegistrationInfo::DeviceRegistrationInfo(
    const std::shared_ptr<CommandManager>& command_manager,
    const std::shared_ptr<StateManager>& state_manager,
    std::unique_ptr<BuffetConfig> config,
    const std::shared_ptr<chromeos::http::Transport>& transport,
    bool notifications_enabled)
    : transport_{transport},
      command_manager_{command_manager},
      state_manager_{state_manager},
      config_{std::move(config)},
      notifications_enabled_{notifications_enabled} {
  cloud_backoff_policy_.reset(new chromeos::BackoffEntry::Policy{});
  cloud_backoff_policy_->num_errors_to_ignore = 0;
  cloud_backoff_policy_->initial_delay_ms = 100;
  cloud_backoff_policy_->multiply_factor = 2.0;
  cloud_backoff_policy_->jitter_factor = 0.1;
  cloud_backoff_policy_->maximum_backoff_ms = 30000;
  cloud_backoff_policy_->entry_lifetime_ms = -1;
  cloud_backoff_policy_->always_use_initial_delay = false;
  cloud_backoff_entry_.reset(
      new chromeos::BackoffEntry{cloud_backoff_policy_.get()});

  command_manager_->AddOnCommandDefChanged(
      base::Bind(&DeviceRegistrationInfo::OnCommandDefsChanged,
                 weak_factory_.GetWeakPtr()));
  state_manager_->AddOnChangedCallback(
      base::Bind(&DeviceRegistrationInfo::OnStateChanged,
                 weak_factory_.GetWeakPtr()));
}

DeviceRegistrationInfo::~DeviceRegistrationInfo() = default;

std::pair<std::string, std::string>
    DeviceRegistrationInfo::GetAuthorizationHeader() const {
  return BuildAuthHeader("Bearer", access_token_);
}

std::string DeviceRegistrationInfo::GetServiceURL(
    const std::string& subpath,
    const chromeos::data_encoding::WebParamList& params) const {
  return BuildURL(config_->service_url(), {subpath}, params);
}

std::string DeviceRegistrationInfo::GetDeviceURL(
    const std::string& subpath,
    const chromeos::data_encoding::WebParamList& params) const {
  CHECK(!config_->device_id().empty()) << "Must have a valid device ID";
  return BuildURL(config_->service_url(),
                  {"devices", config_->device_id(), subpath}, params);
}

std::string DeviceRegistrationInfo::GetOAuthURL(
    const std::string& subpath,
    const chromeos::data_encoding::WebParamList& params) const {
  return BuildURL(config_->oauth_url(), {subpath}, params);
}

void DeviceRegistrationInfo::Start() {
  if (HaveRegistrationCredentials(nullptr)) {
    StartNotificationChannel();
    // Wait a significant amount of time for local daemons to publish their
    // state to Buffet before publishing it to the cloud.
    // TODO(wiley) We could do a lot of things here to either expose this
    //             timeout as a configurable knob or allow local
    //             daemons to signal that their state is up to date so that
    //             we need not wait for them.
    ScheduleStartDevice(base::TimeDelta::FromSeconds(5));
  }
}

void DeviceRegistrationInfo::ScheduleStartDevice(const base::TimeDelta& later) {
  SetRegistrationStatus(RegistrationStatus::kConnecting);
  base::MessageLoop* current = base::MessageLoop::current();
  if (!current)
    return;  // Assume we're in unittests
  base::TimeDelta max_delay =
      base::TimeDelta::FromMinutes(kMaxStartDeviceRetryDelayMinutes);
  base::TimeDelta min_delay =
      base::TimeDelta::FromSeconds(kMinStartDeviceRetryDelaySeconds);
  base::TimeDelta retry_delay = later * 2;
  if (retry_delay > max_delay) { retry_delay = max_delay; }
  if (retry_delay < min_delay) { retry_delay = min_delay; }
  current->PostDelayedTask(
      FROM_HERE,
      base::Bind(&DeviceRegistrationInfo::StartDevice,
                 weak_factory_.GetWeakPtr(), nullptr,
                 retry_delay),
      later);
}

bool DeviceRegistrationInfo::HaveRegistrationCredentials(
    chromeos::ErrorPtr* error) {
  const bool have_credentials = !config_->refresh_token().empty() &&
                                !config_->device_id().empty() &&
                                !config_->robot_account().empty();

  VLOG(1) << "Device registration record "
          << ((have_credentials) ? "found" : "not found.");
  if (!have_credentials)
    chromeos::Error::AddTo(error, FROM_HERE, kErrorDomainGCD,
                           "device_not_registered",
                           "No valid device registration record found");
  return have_credentials;
}

std::unique_ptr<base::DictionaryValue>
DeviceRegistrationInfo::ParseOAuthResponse(chromeos::http::Response* response,
                                           chromeos::ErrorPtr* error) {
  int code = 0;
  auto resp = chromeos::http::ParseJsonResponse(response, &code, error);
  if (resp && code >= chromeos::http::status_code::BadRequest) {
    std::string error_code, error_message;
    if (!resp->GetString("error", &error_code)) {
      error_code = "unexpected_response";
    }
    if (error_code == "invalid_grant") {
      LOG(INFO) << "The device's registration has been revoked.";
      SetRegistrationStatus(RegistrationStatus::kInvalidCredentials);
    }
    // I have never actually seen an error_description returned.
    if (!resp->GetString("error_description", &error_message)) {
      error_message = "Unexpected OAuth error";
    }
    chromeos::Error::AddTo(error, FROM_HERE, buffet::kErrorDomainOAuth2,
                           error_code, error_message);
    return std::unique_ptr<base::DictionaryValue>();
  }
  return resp;
}

void DeviceRegistrationInfo::RefreshAccessToken(
    const base::Closure& success_callback,
    const CloudRequestErrorCallback& error_callback) {
  LOG(INFO) << "Refreshing access token.";
  // Make a shared pointer to |error_callback| since we are going to share this
  // callback with both success and error callbacks for PostFormData() and if
  // |error_callback| has any move-only types, one of the copies will be bad.
  auto shared_error_callback =
      std::make_shared<CloudRequestErrorCallback>(error_callback);

  auto on_refresh_error = [shared_error_callback](
      chromeos::http::RequestID id,
      const chromeos::Error* error) {
    shared_error_callback->Run(error);
  };

  chromeos::http::FormFieldList form_data{
    {"refresh_token", config_->refresh_token()},
    {"client_id", config_->client_id()},
    {"client_secret", config_->client_secret()},
    {"grant_type", "refresh_token"},
  };

  chromeos::http::PostFormData(
      GetOAuthURL("token"), form_data, {}, transport_,
      base::Bind(&DeviceRegistrationInfo::OnRefreshAccessTokenSuccess,
                 weak_factory_.GetWeakPtr(),
                 success_callback, shared_error_callback),
      base::Bind(on_refresh_error));
}

void DeviceRegistrationInfo::OnRefreshAccessTokenSuccess(
    const base::Closure& success_callback,
    const std::shared_ptr<CloudRequestErrorCallback>& error_callback,
    chromeos::http::RequestID id,
    std::unique_ptr<chromeos::http::Response> response) {
  chromeos::ErrorPtr error;
  auto json = ParseOAuthResponse(response.get(), &error);
  if (!json) {
    error_callback->Run(error.get());
    return;
  }

  int expires_in = 0;
  if (!json->GetString("access_token", &access_token_) ||
      !json->GetInteger("expires_in", &expires_in) ||
      access_token_.empty() ||
      expires_in <= 0) {
    LOG(ERROR) << "Access token unavailable.";
    chromeos::Error::AddTo(&error, FROM_HERE, kErrorDomainOAuth2,
                           "unexpected_server_response",
                           "Access token unavailable");
    error_callback->Run(error.get());
    return;
  }
  access_token_expiration_ = base::Time::Now() +
                             base::TimeDelta::FromSeconds(expires_in);
  LOG(INFO) << "Access token is refreshed for additional " << expires_in
            << " seconds.";

  if (primary_notification_channel_ &&
      !primary_notification_channel_->IsConnected()) {
    // If we have disconnected channel, it is due to failed credentials.
    // Now that we have a new access token, retry the connection.
    StartNotificationChannel();
  }
  success_callback.Run();
}

void DeviceRegistrationInfo::StartNotificationChannel() {
  if (notification_channel_starting_)
    return;

  // If no MessageLoop assume we're in unittests.
  if (!base::MessageLoop::current()) {
    LOG(INFO) << "No MessageLoop, not starting notification channel";
    return;
  }

  auto task_runner = base::MessageLoop::current()->task_runner();

  if (primary_notification_channel_) {
    primary_notification_channel_->Stop();
    primary_notification_channel_.reset();
    current_notification_channel_ = nullptr;
  }

  // Start with just regular polling at the pre-configured polling interval.
  // Once the primary notification channel is connected successfully, it will
  // call back to OnConnected() and at that time we'll switch to use the
  // primary channel and switch periodic poll into much more infrequent backup
  // poll mode.
  const base::TimeDelta pull_interval =
      base::TimeDelta::FromMilliseconds(config_->polling_period_ms());
  if (!pull_channel_) {
    pull_channel_.reset(new PullChannel{pull_interval, task_runner});
    pull_channel_->Start(this);
  } else {
    pull_channel_->UpdatePullInterval(pull_interval);
  }
  current_notification_channel_ = pull_channel_.get();

  if (!notifications_enabled_) {
    LOG(WARNING) << "Notification channel disabled by flag.";
    return;
  }

  notification_channel_starting_ = true;
  primary_notification_channel_.reset(
      new XmppChannel{config_->robot_account(), access_token_, task_runner});
  primary_notification_channel_->Start(this);
}

void DeviceRegistrationInfo::AddOnRegistrationChangedCallback(
    const OnRegistrationChangedCallback& callback) {
  on_registration_changed_.push_back(callback);
  callback.Run(registration_status_);
}

std::unique_ptr<base::DictionaryValue>
DeviceRegistrationInfo::BuildDeviceResource(chromeos::ErrorPtr* error) {
  // Limit only to commands that are visible to the cloud.
  auto commands = command_manager_->GetCommandDictionary().GetCommandsAsJson(
      [](const CommandDefinition* def) { return def->GetVisibility().cloud; },
      true, error);
  if (!commands)
    return nullptr;

  std::unique_ptr<base::DictionaryValue> state =
      state_manager_->GetStateValuesAsJson(error);
  if (!state)
    return nullptr;

  std::unique_ptr<base::DictionaryValue> resource{new base::DictionaryValue};
  if (!config_->device_id().empty())
    resource->SetString("id", config_->device_id());
  resource->SetString("name", config_->name());
  if (!config_->description().empty())
    resource->SetString("description", config_->description());
  if (!config_->location().empty())
    resource->SetString("location", config_->location());
  resource->SetString("modelManifestId", config_->model_id());
  resource->SetString("deviceKind", config_->device_kind());
  std::unique_ptr<base::DictionaryValue> channel{new base::DictionaryValue};
  if (current_notification_channel_) {
    channel->SetString("supportedType",
                       current_notification_channel_->GetName());
    current_notification_channel_->AddChannelParameters(channel.get());
  } else {
    channel->SetString("supportedType", "pull");
  }
  resource->Set("channel", channel.release());
  resource->Set("commandDefs", commands.release());
  resource->Set("state", state.release());

  return resource;
}

void DeviceRegistrationInfo::GetDeviceInfo(
    const CloudRequestCallback& success_callback,
    const CloudRequestErrorCallback& error_callback) {
  DoCloudRequest(chromeos::http::request_type::kGet, GetDeviceURL(), nullptr,
                 success_callback, error_callback);
}

std::string DeviceRegistrationInfo::RegisterDevice(const std::string& ticket_id,
                                                   chromeos::ErrorPtr* error) {
  std::unique_ptr<base::DictionaryValue> device_draft =
      BuildDeviceResource(error);
  if (!device_draft)
    return std::string();

  base::DictionaryValue req_json;
  req_json.SetString("id", ticket_id);
  req_json.SetString("oauthClientId", config_->client_id());
  req_json.Set("deviceDraft", device_draft.release());

  auto url = GetServiceURL("registrationTickets/" + ticket_id,
                           {{"key", config_->api_key()}});
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
                      "/finalize?key=" + config_->api_key());
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
  std::string device_id;
  std::string robot_account;
  if (!json_resp->GetString("robotAccountEmail", &robot_account) ||
      !json_resp->GetString("robotAccountAuthorizationCode", &auth_code) ||
      !json_resp->GetString("deviceDraft.id", &device_id)) {
    chromeos::Error::AddTo(error, FROM_HERE, kErrorDomainGCD,
                           "unexpected_response",
                           "Device account missing in response");
    return std::string();
  }

  // Now get access_token and refresh_token
  response = chromeos::http::PostFormDataAndBlock(GetOAuthURL("token"), {
    {"code", auth_code},
    {"client_id", config_->client_id()},
    {"client_secret", config_->client_secret()},
    {"redirect_uri", "oob"},
    {"scope", "https://www.googleapis.com/auth/clouddevices"},
    {"grant_type", "authorization_code"}
  }, {}, transport_, error);
  if (!response)
    return std::string();

  json_resp = ParseOAuthResponse(response.get(), error);
  int expires_in = 0;
  std::string refresh_token;
  if (!json_resp || !json_resp->GetString("access_token", &access_token_) ||
      !json_resp->GetString("refresh_token", &refresh_token) ||
      !json_resp->GetInteger("expires_in", &expires_in) ||
      access_token_.empty() || refresh_token.empty() || expires_in <= 0) {
    chromeos::Error::AddTo(error, FROM_HERE,
                           kErrorDomainGCD, "unexpected_response",
                           "Device access_token missing in response");
    return std::string();
  }

  access_token_expiration_ = base::Time::Now() +
                             base::TimeDelta::FromSeconds(expires_in);

  BuffetConfig::Transaction change{config_.get()};
  change.set_device_id(device_id);
  change.set_robot_account(robot_account);
  change.set_refresh_token(refresh_token);
  change.Commit();

  StartNotificationChannel();

  // We're going to respond with our success immediately and we'll StartDevice
  // shortly after.
  ScheduleStartDevice(base::TimeDelta::FromSeconds(0));
  return device_id;
}

void DeviceRegistrationInfo::DoCloudRequest(
    const std::string& method,
    const std::string& url,
    const base::DictionaryValue* body,
    const CloudRequestCallback& success_callback,
    const CloudRequestErrorCallback& error_callback) {
  // We make CloudRequestData shared here because we want to make sure
  // there is only one instance of success_callback and error_calback since
  // those may have move-only types and making a copy of the callback with
  // move-only types curried-in will invalidate the source callback.
  auto data = std::make_shared<CloudRequestData>();
  data->method = method;
  data->url = url;
  if (body)
    base::JSONWriter::Write(body, &data->body);
  data->success_callback = success_callback;
  data->error_callback = error_callback;
  SendCloudRequest(data);
}

void DeviceRegistrationInfo::SendCloudRequest(
    const std::shared_ptr<const CloudRequestData>& data) {
  // TODO(antonm): Add reauthorization on access token expiration (do not
  // forget about 5xx when fetching new access token).
  // TODO(antonm): Add support for device removal.

  VLOG(1) << "Sending cloud request '" << data->method << "' to '" << data->url
          << "' with request body '" << data->body << "'";
  chromeos::ErrorPtr error;
  if (!HaveRegistrationCredentials(&error)) {
    data->error_callback.Run(error.get());
    return;
  }

  if (cloud_backoff_entry_->ShouldRejectRequest()) {
    VLOG(1) << "Cloud request delayed for "
            << cloud_backoff_entry_->GetTimeUntilRelease()
            << " due to backoff policy";
    base::MessageLoop::current()->PostDelayedTask(
        FROM_HERE,
        base::Bind(&DeviceRegistrationInfo::SendCloudRequest, AsWeakPtr(),
                   data),
        cloud_backoff_entry_->GetTimeUntilRelease());
    return;
  }

  using chromeos::mime::application::kJson;
  using chromeos::mime::parameters::kCharset;
  const std::string mime_type =
      chromeos::mime::AppendParameter(kJson, kCharset, "utf-8");

  chromeos::http::RequestID request_id = chromeos::http::SendRequest(
      data->method, data->url, data->body.c_str(), data->body.size(), mime_type,
      {GetAuthorizationHeader()}, transport_,
      base::Bind(&DeviceRegistrationInfo::OnCloudRequestSuccess, AsWeakPtr(),
                 data),
      base::Bind(&DeviceRegistrationInfo::OnCloudRequestError, AsWeakPtr(),
                 data));
  VLOG(1) << "Cloud request with ID " << request_id << " successfully sent";
}

void DeviceRegistrationInfo::OnCloudRequestSuccess(
    const std::shared_ptr<const CloudRequestData>& data,
    chromeos::http::RequestID request_id,
    std::unique_ptr<chromeos::http::Response> response) {
  int status_code = response->GetStatusCode();
  VLOG(1) << "Response for cloud request with ID " << request_id
          << " received with status code " << status_code;
  if (status_code == chromeos::http::status_code::Denied) {
    RefreshAccessToken(
        base::Bind(&DeviceRegistrationInfo::OnAccessTokenRefreshed, AsWeakPtr(),
                   data),
        base::Bind(&DeviceRegistrationInfo::OnAccessTokenError, AsWeakPtr(),
                   data));
    return;
  }

  if (status_code >= chromeos::http::status_code::InternalServerError) {
    // Request was valid, but server failed, retry.
    // TODO(antonm): Reconsider status codes, maybe only some require
    // retry.
    // TODO(antonm): Support Retry-After header.
    RetryCloudRequest(data);
    return;
  }

  cloud_backoff_entry_->InformOfRequest(true);

  chromeos::ErrorPtr error;
  auto json_resp = chromeos::http::ParseJsonResponse(response.get(), nullptr,
                                                     &error);
  if (!json_resp) {
    data->error_callback.Run(error.get());
    return;
  }

  if (!response->IsSuccessful()) {
    ParseGCDError(json_resp.get(), &error);
    data->error_callback.Run(error.get());
    return;
  }

  SetRegistrationStatus(RegistrationStatus::kConnected);
  data->success_callback.Run(*json_resp);
}

void DeviceRegistrationInfo::OnCloudRequestError(
    const std::shared_ptr<const CloudRequestData>& data,
    chromeos::http::RequestID request_id,
    const chromeos::Error* error) {
  VLOG(1) << "Cloud request with ID " << request_id << " failed";
  RetryCloudRequest(data);
}

void DeviceRegistrationInfo::RetryCloudRequest(
    const std::shared_ptr<const CloudRequestData>& data) {
  SetRegistrationStatus(RegistrationStatus::kConnecting);
  cloud_backoff_entry_->InformOfRequest(false);
  SendCloudRequest(data);
}

void DeviceRegistrationInfo::OnAccessTokenRefreshed(
    const std::shared_ptr<const CloudRequestData>& data) {
  SendCloudRequest(data);
}

void DeviceRegistrationInfo::OnAccessTokenError(
    const std::shared_ptr<const CloudRequestData>& data,
    const chromeos::Error* error) {
  data->error_callback.Run(error);
}

void DeviceRegistrationInfo::StartDevice(
    chromeos::ErrorPtr* error,
    const base::TimeDelta& retry_delay) {
  if (!HaveRegistrationCredentials(error))
    return;
  auto handle_start_device_failure_cb = base::Bind(
      &IgnoreCloudErrorWithCallback,
      base::Bind(&DeviceRegistrationInfo::ScheduleStartDevice,
                 weak_factory_.GetWeakPtr(),
                 retry_delay));
  // "Starting" a device just means that we:
  //   1) push an updated device resource
  //   2) fetch an initial set of outstanding commands
  //   3) abort any commands that we've previously marked as "in progress"
  //      or as being in an error state; publish queued commands
  auto abort_commands_cb = base::Bind(
      &DeviceRegistrationInfo::ProcessInitialCommandList,
      weak_factory_.GetWeakPtr());
  auto fetch_commands_cb = base::Bind(
      &DeviceRegistrationInfo::FetchCommands,
      weak_factory_.GetWeakPtr(),
      abort_commands_cb,
      handle_start_device_failure_cb);
  UpdateDeviceResource(fetch_commands_cb, handle_start_device_failure_cb);
}

bool DeviceRegistrationInfo::UpdateDeviceInfo(const std::string& name,
                                              const std::string& description,
                                              const std::string& location,
                                              chromeos::ErrorPtr* error) {
  BuffetConfig::Transaction change{config_.get()};
  if (!change.set_name(name)) {
    chromeos::Error::AddTo(error, FROM_HERE, kErrorDomainBuffet,
                           "invalid_parameter", "Empty device name");
    return false;
  }
  change.set_description(description);
  change.set_location(location);
  change.Commit();

  if (HaveRegistrationCredentials(nullptr)) {
    UpdateDeviceResource(base::Bind(&base::DoNothing),
                         base::Bind(&IgnoreCloudError));
  }

  return true;
}

bool DeviceRegistrationInfo::UpdateBaseConfig(
    const std::string& anonymous_access_role,
    bool local_discovery_enabled,
    bool local_pairing_enabled,
    chromeos::ErrorPtr* error) {
  BuffetConfig::Transaction change(config_.get());
  if (!change.set_local_anonymous_access_role(anonymous_access_role)) {
    chromeos::Error::AddToPrintf(error, FROM_HERE, kErrorDomainBuffet,
                                 "invalid_parameter", "Invalid role: %s",
                                 anonymous_access_role.c_str());
    return false;
  }

  change.set_local_discovery_enabled(local_discovery_enabled);
  change.set_local_pairing_enabled(local_pairing_enabled);

  return true;
}

bool DeviceRegistrationInfo::UpdateServiceConfig(
    const std::string& client_id,
    const std::string& client_secret,
    const std::string& api_key,
    const std::string& oauth_url,
    const std::string& service_url,
    chromeos::ErrorPtr* error) {
  if (HaveRegistrationCredentials(nullptr)) {
    chromeos::Error::AddTo(error, FROM_HERE, kErrorDomainBuffet,
                           "already_registered",
                           "Unable to change config for registered device");
    return false;
  }
  BuffetConfig::Transaction change{config_.get()};
  change.set_client_id(client_id);
  change.set_client_secret(client_secret);
  change.set_api_key(api_key);
  change.set_oauth_url(oauth_url);
  change.set_service_url(service_url);
  return true;
}

void DeviceRegistrationInfo::UpdateCommand(
    const std::string& command_id,
    const base::DictionaryValue& command_patch,
    const base::Closure& on_success,
    const base::Closure& on_error) {
  DoCloudRequest(
      chromeos::http::request_type::kPatch,
      GetServiceURL("commands/" + command_id),
      &command_patch,
      base::Bind(&IgnoreCloudResultWithCallback, on_success),
      base::Bind(&IgnoreCloudErrorWithCallback, on_error));
}

void DeviceRegistrationInfo::NotifyCommandAborted(
    const std::string& command_id,
    chromeos::ErrorPtr error) {
  base::DictionaryValue command_patch;
  command_patch.SetString(commands::attributes::kCommand_State,
                          CommandInstance::kStatusAborted);
  if (error) {
    command_patch.SetString(commands::attributes::kCommand_ErrorCode,
                            chromeos::string_utils::Join(":",
                                                         error->GetDomain(),
                                                         error->GetCode()));
    std::vector<std::string> messages;
    const chromeos::Error* current_error = error.get();
    while (current_error) {
      messages.push_back(current_error->GetMessage());
      current_error = current_error->GetInnerError();
    }
    command_patch.SetString(commands::attributes::kCommand_ErrorMessage,
                            chromeos::string_utils::Join(";", messages));
  }
  UpdateCommand(command_id,
                command_patch,
                base::Bind(&base::DoNothing),
                base::Bind(&DeviceRegistrationInfo::RetryNotifyCommandAborted,
                           weak_factory_.GetWeakPtr(),
                           command_id, base::Passed(std::move(error))));
}

void DeviceRegistrationInfo::RetryNotifyCommandAborted(
    const std::string& command_id,
    chromeos::ErrorPtr error) {
  base::MessageLoop::current()->PostDelayedTask(
      FROM_HERE,
      base::Bind(&DeviceRegistrationInfo::NotifyCommandAborted,
                 weak_factory_.GetWeakPtr(),
                 command_id, base::Passed(std::move(error))),
      base::TimeDelta::FromSeconds(kAbortCommandRetryDelaySeconds));
}

void DeviceRegistrationInfo::UpdateDeviceResource(
    const base::Closure& on_success,
    const CloudRequestErrorCallback& on_failure) {
  VLOG(1) << "Updating GCD server with CDD...";
  std::unique_ptr<base::DictionaryValue> device_resource =
      BuildDeviceResource(nullptr);
  if (!device_resource)
    return;

  DoCloudRequest(
      chromeos::http::request_type::kPut,
      GetDeviceURL(),
      device_resource.get(),
      base::Bind(&IgnoreCloudResultWithCallback, on_success),
      on_failure);
}

namespace {

void HandleFetchCommandsResult(
    const base::Callback<void(const base::ListValue&)>& callback,
    const base::DictionaryValue& json) {
  const base::ListValue* commands{nullptr};
  if (!json.GetList("commands", &commands)) {
    VLOG(1) << "No commands in the response.";
  }
  const base::ListValue empty;
  callback.Run(commands ? *commands : empty);
}

}  // namespace

void DeviceRegistrationInfo::FetchCommands(
    const base::Callback<void(const base::ListValue&)>& on_success,
    const CloudRequestErrorCallback& on_failure) {
  DoCloudRequest(
      chromeos::http::request_type::kGet,
      GetServiceURL("commands/queue", {{"deviceId", config_->device_id()}}),
      nullptr, base::Bind(&HandleFetchCommandsResult, on_success), on_failure);
}

void DeviceRegistrationInfo::ProcessInitialCommandList(
    const base::ListValue& commands) {
  for (const base::Value* command : commands) {
    const base::DictionaryValue* command_dict{nullptr};
    if (!command->GetAsDictionary(&command_dict)) {
      LOG(WARNING) << "Not a command dictionary: " << *command;
      continue;
    }
    std::string command_state;
    if (!command_dict->GetString("state", &command_state)) {
      LOG(WARNING) << "Command with no state at " << *command;
      continue;
    }
    if (command_state == "error" &&
        command_state == "inProgress" &&
        command_state == "paused") {
      // It's a limbo command, abort it.
      std::string command_id;
      if (!command_dict->GetString("id", &command_id)) {
        LOG(WARNING) << "Command with no ID at " << *command;
        continue;
      }

      std::unique_ptr<base::DictionaryValue> cmd_copy{command_dict->DeepCopy()};
      cmd_copy->SetString("state", "aborted");
      // TODO(wiley) We could consider handling this error case more gracefully.
      DoCloudRequest(
          chromeos::http::request_type::kPut,
          GetServiceURL("commands/" + command_id),
          cmd_copy.get(),
          base::Bind(&IgnoreCloudResult), base::Bind(&IgnoreCloudError));
    } else {
      // Normal command, publish it to local clients.
      PublishCommand(*command_dict);
    }
  }
}

void DeviceRegistrationInfo::PublishCommands(const base::ListValue& commands) {
  for (const base::Value* command : commands) {
    const base::DictionaryValue* command_dict{nullptr};
    if (!command->GetAsDictionary(&command_dict)) {
      LOG(WARNING) << "Not a command dictionary: " << *command;
      continue;
    }
    PublishCommand(*command_dict);
  }
}

void DeviceRegistrationInfo::PublishCommand(
    const base::DictionaryValue& command) {
  std::string command_id;
  chromeos::ErrorPtr error;
  auto command_instance = CommandInstance::FromJson(
      &command, commands::attributes::kCommand_Visibility_Cloud,
      command_manager_->GetCommandDictionary(), &command_id, &error);
  if (!command_instance) {
    LOG(WARNING) << "Failed to parse a command instance: " << command;
    if (!command_id.empty())
      NotifyCommandAborted(command_id, std::move(error));
    return;
  }

  // TODO(antonm): Properly process cancellation of commands.
  if (!command_manager_->FindCommand(command_instance->GetID())) {
    LOG(INFO) << "New command '" << command_instance->GetName()
              << "' arrived, ID: " << command_instance->GetID();
    std::unique_ptr<CommandProxyInterface> cloud_proxy{
        new CloudCommandProxy(command_instance.get(), this)};
    command_instance->AddProxy(std::move(cloud_proxy));
    command_manager_->AddCommand(std::move(command_instance));
  }
}

void DeviceRegistrationInfo::PublishStateUpdates() {
  // If we have pending state update requests, don't send any more for now.
  if (device_state_update_pending_)
    return;

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

  device_state_update_pending_ = true;
  DoCloudRequest(
      chromeos::http::request_type::kPost, GetDeviceURL("patchState"), &body,
      base::Bind(&DeviceRegistrationInfo::OnPublishStateSuccess, AsWeakPtr()),
      base::Bind(&DeviceRegistrationInfo::OnPublishStateError, AsWeakPtr()));
}

void DeviceRegistrationInfo::OnPublishStateSuccess(
    const base::DictionaryValue& reply) {
  device_state_update_pending_ = false;
  // See if there were more pending state updates since the previous request
  // had been sent out.
  PublishStateUpdates();
}

void DeviceRegistrationInfo::OnPublishStateError(const chromeos::Error* error) {
  LOG(ERROR) << "Permanent failure while trying to update device state";
  device_state_update_pending_ = false;
}

void DeviceRegistrationInfo::SetRegistrationStatus(
    RegistrationStatus new_status) {
  VLOG_IF(1, new_status != registration_status_)
      << "Changing registration status to " << StatusToString(new_status);
  registration_status_ = new_status;
  for (const auto& cb : on_registration_changed_)
    cb.Run(registration_status_);
}

void DeviceRegistrationInfo::OnCommandDefsChanged() {
  VLOG(1) << "CommandDefinitionChanged notification received";
  if (!HaveRegistrationCredentials(nullptr))
    return;

  UpdateDeviceResource(base::Bind(&base::DoNothing),
                       base::Bind(&IgnoreCloudError));
}

void DeviceRegistrationInfo::OnStateChanged() {
  VLOG(1) << "StateChanged notification received";
  if (!HaveRegistrationCredentials(nullptr))
    return;

  // TODO(vitalybuka): Integrate BackoffEntry.
  PublishStateUpdates();
}

void DeviceRegistrationInfo::OnConnected(const std::string& channel_name) {
  LOG(INFO) << "Notification channel successfully established over "
            << channel_name;
  CHECK_EQ(primary_notification_channel_->GetName(), channel_name);
  notification_channel_starting_ = false;
  pull_channel_->UpdatePullInterval(
      base::TimeDelta::FromMilliseconds(config_->backup_polling_period_ms()));
  current_notification_channel_ = primary_notification_channel_.get();
  UpdateDeviceResource(base::Bind(&base::DoNothing),
                       base::Bind(&IgnoreCloudError));
}

void DeviceRegistrationInfo::OnDisconnected() {
  LOG(INFO) << "Notification channel disconnected";
  pull_channel_->UpdatePullInterval(
      base::TimeDelta::FromMilliseconds(config_->polling_period_ms()));
  current_notification_channel_ = pull_channel_.get();
  UpdateDeviceResource(base::Bind(&base::DoNothing),
                       base::Bind(&IgnoreCloudError));
}

void DeviceRegistrationInfo::OnPermanentFailure() {
  LOG(ERROR) << "Failed to establish notification channel.";
  notification_channel_starting_ = false;
  RefreshAccessToken(base::Bind(&base::DoNothing),
                     base::Bind(&IgnoreCloudError));
}

void DeviceRegistrationInfo::OnCommandCreated(
    const base::DictionaryValue& command) {
  if (!command.empty()) {
    // GCD spec indicates that the command parameter in notification object
    // "may be empty if command size is too big".
    PublishCommand(command);
    return;
  }
  // If the command was too big to be delivered over a notification channel,
  // or OnCommandCreated() was initiated from the Pull notification,
  // perform a manual command fetch from the server here.
  FetchCommands(base::Bind(&DeviceRegistrationInfo::PublishCommands,
                           weak_factory_.GetWeakPtr()),
                base::Bind(&IgnoreCloudError));
}


}  // namespace buffet
