// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBWEAVE_SRC_DEVICE_REGISTRATION_INFO_H_
#define LIBWEAVE_SRC_DEVICE_REGISTRATION_INFO_H_

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/callback.h>
#include <base/macros.h>
#include <base/memory/weak_ptr.h>
#include <base/time/time.h>
#include <weave/error.h>
#include <weave/cloud.h>
#include <weave/http_client.h>

#include "libweave/src/backoff_entry.h"
#include "libweave/src/commands/cloud_command_update_interface.h"
#include "libweave/src/commands/command_manager.h"
#include "libweave/src/config.h"
#include "libweave/src/data_encoding.h"
#include "libweave/src/notification/notification_channel.h"
#include "libweave/src/notification/notification_delegate.h"
#include "libweave/src/notification/pull_channel.h"
#include "libweave/src/states/state_change_queue_interface.h"

namespace base {
class DictionaryValue;
}  // namespace base

namespace chromeos {
class KeyValueStore;
}  // namespace chromeos

namespace weave {

class Network;
class StateManager;
class TaskRunner;

extern const char kErrorDomainOAuth2[];
extern const char kErrorDomainGCD[];
extern const char kErrorDomainGCDServer[];

// The DeviceRegistrationInfo class represents device registration information.
class DeviceRegistrationInfo : public Cloud,
                               public NotificationDelegate,
                               public CloudCommandUpdateInterface {
 public:
  using CloudRequestCallback =
      base::Callback<void(const base::DictionaryValue& response)>;
  using CloudRequestErrorCallback = base::Callback<void(const Error* error)>;

  DeviceRegistrationInfo(const std::shared_ptr<CommandManager>& command_manager,
                         const std::shared_ptr<StateManager>& state_manager,
                         std::unique_ptr<Config> config,
                         TaskRunner* task_runner,
                         HttpClient* http_client,
                         bool notifications_enabled,
                         weave::Network* network);

  ~DeviceRegistrationInfo() override;

  // Cloud overrides.
  void AddOnRegistrationChangedCallback(
      const OnRegistrationChangedCallback& callback) override;
  void GetDeviceInfo(
      const OnCloudRequestCallback& success_callback,
      const OnCloudRequestErrorCallback& error_callback) override;
  std::string RegisterDevice(const std::string& ticket_id,
                             ErrorPtr* error) override;
  bool UpdateDeviceInfo(const std::string& name,
                        const std::string& description,
                        const std::string& location,
                        ErrorPtr* error) override;
  bool UpdateBaseConfig(const std::string& anonymous_access_role,
                        bool local_discovery_enabled,
                        bool local_pairing_enabled,
                        ErrorPtr* error) override;
  bool UpdateServiceConfig(const std::string& client_id,
                           const std::string& client_secret,
                           const std::string& api_key,
                           const std::string& oauth_url,
                           const std::string& service_url,
                           ErrorPtr* error) override;

  // Add callback to listen for changes in config.
  void AddOnConfigChangedCallback(const Config::OnChangedCallback& callback);

  // Returns the GCD service request URL. If |subpath| is specified, it is
  // appended to the base URL which is normally
  //    https://www.googleapis.com/clouddevices/v1/".
  // If |params| are specified, each key-value pair is formatted using
  // WebParamsEncode() and appended to URL as a query
  // string.
  // So, calling:
  //    GetServiceURL("ticket", {{"key","apiKey"}})
  // will return something like:
  //    https://www.googleapis.com/clouddevices/v1/ticket?key=apiKey
  std::string GetServiceURL(const std::string& subpath = {},
                            const WebParamList& params = {}) const;

  // Returns a service URL to access the registered device on GCD server.
  // The base URL used to construct the full URL looks like this:
  //    https://www.googleapis.com/clouddevices/v1/devices/<device_id>/
  std::string GetDeviceURL(const std::string& subpath = {},
                           const WebParamList& params = {}) const;

  // Similar to GetServiceURL, GetOAuthURL() returns a URL of OAuth 2.0 server.
  // The base URL used is https://accounts.google.com/o/oauth2/.
  std::string GetOAuthURL(const std::string& subpath = {},
                          const WebParamList& params = {}) const;

  // Starts GCD device if credentials available.
  void Start();

  // Checks whether we have credentials generated during registration.
  bool HaveRegistrationCredentials() const;
  // Calls HaveRegistrationCredentials() and logs an error if no credentials
  // are available.
  bool VerifyRegistrationCredentials(ErrorPtr* error) const;

  // Updates a command (override from CloudCommandUpdateInterface).
  void UpdateCommand(const std::string& command_id,
                     const base::DictionaryValue& command_patch,
                     const base::Closure& on_success,
                     const base::Closure& on_error) override;

  // TODO(vitalybuka): remove getters and pass config to dependent code.
  const Config& GetConfig() const { return *config_; }
  Config* GetMutableConfig() { return config_.get(); }

 private:
  friend class DeviceRegistrationInfoTest;

  base::WeakPtr<DeviceRegistrationInfo> AsWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }

  // Cause DeviceRegistrationInfo to attempt to connect to cloud server on
  // its own later.
  void ScheduleCloudConnection(const base::TimeDelta& delay);

  // Initiates the connection to the cloud server.
  // Device will do required start up chores and then start to listen
  // to new commands.
  void ConnectToCloud();
  // Notification called when ConnectToCloud() succeeds.
  void OnConnectedToCloud();

  // Forcibly refreshes the access token.
  void RefreshAccessToken(const base::Closure& success_callback,
                          const CloudRequestErrorCallback& error_callback);

  // Callbacks for RefreshAccessToken().
  void OnRefreshAccessTokenSuccess(
      const std::shared_ptr<base::Closure>& success_callback,
      const std::shared_ptr<CloudRequestErrorCallback>& error_callback,
      int id,
      const HttpClient::Response& response);
  void OnRefreshAccessTokenError(
      const std::shared_ptr<base::Closure>& success_callback,
      const std::shared_ptr<CloudRequestErrorCallback>& error_callback,
      int id,
      const Error* error);

  // Parse the OAuth response, and sets registration status to
  // kInvalidCredentials if our registration is no longer valid.
  std::unique_ptr<base::DictionaryValue> ParseOAuthResponse(
      const HttpClient::Response& response,
      ErrorPtr* error);

  // This attempts to open a notification channel. The channel needs to be
  // restarted anytime the access_token is refreshed.
  void StartNotificationChannel();

  // Do a HTTPS request to cloud services.
  // Handles many cases like reauthorization, 5xx HTTP response codes
  // and device removal.  It is a recommended way to do cloud API
  // requests.
  // TODO(antonm): Consider moving into some other class.
  void DoCloudRequest(const std::string& method,
                      const std::string& url,
                      const base::DictionaryValue* body,
                      const CloudRequestCallback& success_callback,
                      const CloudRequestErrorCallback& error_callback);

  // Helper for DoCloudRequest().
  struct CloudRequestData {
    std::string method;
    std::string url;
    std::string body;
    CloudRequestCallback success_callback;
    CloudRequestErrorCallback error_callback;
  };
  void SendCloudRequest(const std::shared_ptr<const CloudRequestData>& data);
  void OnCloudRequestSuccess(
      const std::shared_ptr<const CloudRequestData>& data,
      int request_id,
      const HttpClient::Response& response);
  void OnCloudRequestError(const std::shared_ptr<const CloudRequestData>& data,
                           int request_id,
                           const Error* error);
  void RetryCloudRequest(const std::shared_ptr<const CloudRequestData>& data);
  void OnAccessTokenRefreshed(
      const std::shared_ptr<const CloudRequestData>& data);
  void OnAccessTokenError(const std::shared_ptr<const CloudRequestData>& data,
                          const Error* error);
  void CheckAccessTokenError(const Error* error);

  void UpdateDeviceResource(const base::Closure& on_success,
                            const CloudRequestErrorCallback& on_failure);
  void StartQueuedUpdateDeviceResource();
  // Success/failure callbacks for UpdateDeviceResource().
  void OnUpdateDeviceResourceSuccess(const base::DictionaryValue& device_info);
  void OnUpdateDeviceResourceError(const Error* error);

  // Callback from GetDeviceInfo() to retrieve the device resource timestamp
  // and retry UpdateDeviceResource() call.
  void OnDeviceInfoRetrieved(const base::DictionaryValue& device_info);

  // Extracts the timestamp from the device resource and sets it to
  // |last_device_resource_updated_timestamp_|.
  // Returns false if the "lastUpdateTimeMs" field is not found in the device
  // resource or it is invalid.
  bool UpdateDeviceInfoTimestamp(const base::DictionaryValue& device_info);

  void FetchCommands(
      const base::Callback<void(const base::ListValue&)>& on_success,
      const CloudRequestErrorCallback& on_failure);
  // Success/failure callbacks for FetchCommands().
  void OnFetchCommandsSuccess(
      const base::Callback<void(const base::ListValue&)>& callback,
      const base::DictionaryValue& json);
  void OnFetchCommandsError(const CloudRequestErrorCallback& callback,
                            const Error* error);
  // Called when FetchCommands completes (with either success or error).
  // This method reschedules any pending/queued fetch requests.
  void OnFetchCommandsReturned();

  // Processes the command list that is fetched from the server on connection.
  // Aborts commands which are in transitional states and publishes queued
  // commands which are queued.
  void ProcessInitialCommandList(const base::ListValue& commands);

  void PublishCommands(const base::ListValue& commands);
  void PublishCommand(const base::DictionaryValue& command);

  // Helper function to pull the pending command list from the server using
  // FetchCommands() and make them available on D-Bus with PublishCommands().
  void FetchAndPublishCommands();

  void PublishStateUpdates();
  void OnPublishStateSuccess(StateChangeQueueInterface::UpdateID update_id,
                             const base::DictionaryValue& reply);
  void OnPublishStateError(const Error* error);

  // If unrecoverable error occurred (e.g. error parsing command instance),
  // notify the server that the command is aborted by the device.
  void NotifyCommandAborted(const std::string& command_id, ErrorPtr error);

  // Builds Cloud API devices collection REST resource which matches
  // current state of the device including command definitions
  // for all supported commands and current device state.
  std::unique_ptr<base::DictionaryValue> BuildDeviceResource(ErrorPtr* error);

  void SetRegistrationStatus(RegistrationStatus new_status);
  void SetDeviceId(const std::string& device_id);

  // Callback called when command definitions are changed to re-publish new CDD.
  void OnCommandDefsChanged();
  void OnStateChanged();

  // Overrides from NotificationDelegate.
  void OnConnected(const std::string& channel_name) override;
  void OnDisconnected() override;
  void OnPermanentFailure() override;
  void OnCommandCreated(const base::DictionaryValue& command) override;
  void OnDeviceDeleted(const std::string& device_id) override;

  // Wipes out the device registration information and stops server connections.
  void MarkDeviceUnregistered();

  // Transient data
  std::string access_token_;
  base::Time access_token_expiration_;
  // The time stamp of last device resource update on the server.
  std::string last_device_resource_updated_timestamp_;
  // Set to true if the device has connected to the cloud server correctly.
  // At this point, normal state and command updates can be dispatched to the
  // server.
  bool connected_to_cloud_{false};

  // HTTP transport used for communications.
  HttpClient* http_client_{nullptr};

  TaskRunner* task_runner_{nullptr};
  // Global command manager.
  std::shared_ptr<CommandManager> command_manager_;
  // Device state manager.
  std::shared_ptr<StateManager> state_manager_;

  std::unique_ptr<Config> config_;

  // Backoff manager for DoCloudRequest() method.
  std::unique_ptr<BackoffEntry::Policy> cloud_backoff_policy_;
  std::unique_ptr<BackoffEntry> cloud_backoff_entry_;
  std::unique_ptr<BackoffEntry> oauth2_backoff_entry_;

  // Flag set to true while a device state update patch request is in flight
  // to the cloud server.
  bool device_state_update_pending_{false};

  // Set to true when command queue fetch request is in flight to the server.
  bool fetch_commands_request_sent_{false};
  // Set to true when another command queue fetch request is queued while
  // another one was in flight.
  bool fetch_commands_request_queued_{false};

  using ResourceUpdateCallbackList =
      std::vector<std::pair<base::Closure, CloudRequestErrorCallback>>;
  // Success/error callbacks for device resource update request currently in
  // flight to the cloud server.
  ResourceUpdateCallbackList in_progress_resource_update_callbacks_;
  // Success/error callbacks for device resource update requests queued while
  // another request is in flight to the cloud server.
  ResourceUpdateCallbackList queued_resource_update_callbacks_;

  const bool notifications_enabled_;
  std::unique_ptr<NotificationChannel> primary_notification_channel_;
  std::unique_ptr<PullChannel> pull_channel_;
  NotificationChannel* current_notification_channel_{nullptr};
  bool notification_channel_starting_{false};

  weave::Network* network_{nullptr};

  // Tracks our current registration status.
  RegistrationStatus registration_status_{RegistrationStatus::kUnconfigured};

  std::vector<OnRegistrationChangedCallback> on_registration_changed_;

  base::WeakPtrFactory<DeviceRegistrationInfo> weak_factory_{this};
  DISALLOW_COPY_AND_ASSIGN(DeviceRegistrationInfo);
};

}  // namespace weave

#endif  // LIBWEAVE_SRC_DEVICE_REGISTRATION_INFO_H_
