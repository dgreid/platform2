// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BUFFET_BUFFET_CONFIG_H_
#define BUFFET_BUFFET_CONFIG_H_

#include <string>
#include <vector>

#include <base/callback.h>
#include <base/files/file_path.h>
#include <chromeos/errors/error.h>
#include <chromeos/key_value_store.h>

namespace buffet {

class StorageInterface;

// Handles reading buffet config and state files.
class BuffetConfig final {
 public:
  using OnChangedCallback = base::Callback<void(const BuffetConfig&)>;

  explicit BuffetConfig(std::unique_ptr<StorageInterface> storage);

  explicit BuffetConfig(const base::FilePath& state_path);

  void AddOnChangedCallback(const OnChangedCallback& callback) {
    on_changed_.push_back(callback);
    // Force to read current state.
    callback.Run(*this);
  }

  void Load(const base::FilePath& config_path);
  void Load(const chromeos::KeyValueStore& store);

  // Allows editing of config. Makes sure that callbacks were called and changes
  // were saved.
  // User can commit changes by calling Commit method or by destroying the
  // object.
  class Transaction final {
   public:
    explicit Transaction(BuffetConfig* config) : config_(config) {
      CHECK(config_);
    }

    ~Transaction();

    void set_client_id(const std::string& id) { config_->client_id_ = id; }
    void set_client_secret(const std::string& secret) {
      config_->client_secret_ = secret;
    }
    void set_api_key(const std::string& key) { config_->api_key_ = key; }
    void set_oauth_url(const std::string& url) { config_->oauth_url_ = url; }
    void set_service_url(const std::string& url) {
      config_->service_url_ = url;
    }
    bool set_name(const std::string& name);
    void set_description(const std::string& description) {
      config_->description_ = description;
    }
    void set_location(const std::string& location) {
      config_->location_ = location;
    }
    bool set_local_anonymous_access_role(const std::string& role);
    void set_local_discovery_enabled(bool enabled) {
      config_->local_discovery_enabled_ = enabled;
    }
    void set_local_pairing_enabled(bool enabled) {
      config_->local_pairing_enabled_ = enabled;
    }
    void set_device_id(const std::string& id) { config_->device_id_ = id; }
    void set_refresh_token(const std::string& token) {
      config_->refresh_token_ = token;
    }
    void set_robot_account(const std::string& account) {
      config_->robot_account_ = account;
    }

    void Commit();

   private:
    friend class BuffetConfig;
    void LoadState();
    BuffetConfig* config_;
    bool save_{true};
  };

  const std::string& client_id() const { return client_id_; }
  const std::string& client_secret() const { return client_secret_; }
  const std::string& api_key() const { return api_key_; }
  const std::string& oauth_url() const { return oauth_url_; }
  const std::string& service_url() const { return service_url_; }
  const std::string& oem_name() const { return oem_name_; }
  const std::string& model_name() const { return model_name_; }
  const std::string& model_id() const { return model_id_; }
  const std::string& device_kind() const { return device_kind_; }
  uint64_t polling_period_ms() const { return polling_period_ms_; }
  uint64_t backup_polling_period_ms() const {
    return backup_polling_period_ms_;
  }

  const std::string& name() const { return name_; }
  const std::string& description() const { return description_; }
  const std::string& location() const { return location_; }
  std::string local_anonymous_access_role() const {
    return local_anonymous_access_role_;
  }
  bool local_pairing_enabled() const { return local_pairing_enabled_; }
  bool local_discovery_enabled() const { return local_discovery_enabled_; }

  std::string device_id() const { return device_id_; }
  std::string refresh_token() const { return refresh_token_; }
  std::string robot_account() const { return robot_account_; }

 private:
  bool Save();

  std::string client_id_{"58855907228.apps.googleusercontent.com"};
  std::string client_secret_{"eHSAREAHrIqPsHBxCE9zPPBi"};
  std::string api_key_{"AIzaSyDSq46gG-AxUnC3zoqD9COIPrjolFsMfMA"};
  std::string oauth_url_{"https://accounts.google.com/o/oauth2/"};
  std::string service_url_{"https://www.googleapis.com/clouddevices/v1/"};
  std::string name_{"Developer device"};
  std::string description_;
  std::string location_;
  std::string local_anonymous_access_role_{"viewer"};
  bool local_discovery_enabled_{true};
  bool local_pairing_enabled_{true};
  std::string oem_name_{"Chromium"};
  std::string model_name_{"Brillo"};
  std::string model_id_{"AAAAA"};
  std::string device_kind_{"vendor"};
  uint64_t polling_period_ms_{7000};  // 7 seconds.
  uint64_t backup_polling_period_ms_{30 * 60 * 1000};  // 30 minutes.

  std::string device_id_;
  std::string refresh_token_;
  std::string robot_account_;

  // Serialization interface to save and load buffet state.
  std::unique_ptr<StorageInterface> storage_;

  std::vector<OnChangedCallback> on_changed_;

  DISALLOW_COPY_AND_ASSIGN(BuffetConfig);
};

}  // namespace buffet

#endif  // BUFFET_BUFFET_CONFIG_H_
