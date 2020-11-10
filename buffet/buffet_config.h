// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BUFFET_BUFFET_CONFIG_H_
#define BUFFET_BUFFET_CONFIG_H_

#include <map>
#include <set>
#include <string>
#include <vector>

#include <base/callback.h>
#include <base/files/file_path.h>
#include <brillo/key_value_store.h>
#include <weave/provider/config_store.h>

namespace buffet {

class StorageInterface;

// Handles reading buffet config and state files.
class BuffetConfig final : public weave::provider::ConfigStore {
 public:
  struct Options {
    std::string client_id;
    std::string client_secret;
    std::string api_key;
    std::string oauth_url;
    std::string service_url;

    base::FilePath defaults;
    base::FilePath settings;

    base::FilePath definitions;
    base::FilePath test_definitions;

    bool disable_security{false};
    std::string test_privet_ssid;
  };

  ~BuffetConfig() override = default;

  explicit BuffetConfig(const Options& options);
  BuffetConfig(const BuffetConfig&) = delete;
  BuffetConfig& operator=(const BuffetConfig&) = delete;

  // Config overrides.
  bool LoadDefaults(weave::Settings* settings) override;
  std::string LoadSettings() override;
  void SaveSettings(const std::string& settings) override;

  bool LoadDefaults(const brillo::KeyValueStore& store,
                    weave::Settings* settings);

 private:
  Options options_;
};

}  // namespace buffet

#endif  // BUFFET_BUFFET_CONFIG_H_
