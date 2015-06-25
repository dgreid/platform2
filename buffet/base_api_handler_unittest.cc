// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "buffet/base_api_handler.h"

#include <base/strings/string_number_conversions.h>
#include <base/values.h>
#include <chromeos/http/http_transport_fake.h>
#include <gtest/gtest.h>

#include "buffet/buffet_config.h"
#include "buffet/commands/command_manager.h"
#include "buffet/commands/unittest_utils.h"
#include "buffet/device_registration_info.h"
#include "buffet/states/mock_state_change_queue_interface.h"
#include "buffet/states/state_manager.h"
#include "buffet/storage_impls.h"

namespace buffet {

class BaseApiHandlerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    transport_ = std::make_shared<chromeos::http::fake::Transport>();
    command_manager_ = std::make_shared<CommandManager>();
    state_manager_ = std::make_shared<StateManager>(&mock_state_change_queue_);
    auto state_definition = unittests::CreateDictionaryValue(R"({
      'base': {
        'firmwareVersion': 'string',
        'localDiscoveryEnabled': 'boolean',
        'localAnonymousAccessMaxRole': [ 'none', 'viewer', 'user' ],
        'localPairingEnabled': 'boolean',
        'network': {
          'properties': {
            'name': 'string'
          }
        }
      }
    })");
    auto state_defaults = unittests::CreateDictionaryValue(R"({
      'base': {
        'firmwareVersion': '123123',
        'localDiscoveryEnabled': false,
        'localAnonymousAccessMaxRole': 'none',
        'localPairingEnabled': false
      }
    })");
    ASSERT_TRUE(state_manager_->LoadStateDefinition(*state_definition, "base",
                                                    nullptr));
    ASSERT_TRUE(state_manager_->LoadStateDefaults(*state_defaults, nullptr));
    dev_reg_.reset(new DeviceRegistrationInfo(
        command_manager_, state_manager_,
        std::unique_ptr<BuffetConfig>{new BuffetConfig{
            std::unique_ptr<StorageInterface>{new MemStorage}}},
        transport_, true, nullptr));
    handler_.reset(new BaseApiHandler{
        dev_reg_->AsWeakPtr(), state_manager_, command_manager_});
  }

  void LoadCommands(const std::string& command_definitions) {
    auto json = unittests::CreateDictionaryValue(command_definitions.c_str());
    EXPECT_TRUE(command_manager_->LoadBaseCommands(*json, nullptr));
    EXPECT_TRUE(command_manager_->LoadCommands(*json, "", nullptr));
  }

  void AddCommand(const std::string& command) {
    auto command_instance = buffet::CommandInstance::FromJson(
        unittests::CreateDictionaryValue(command.c_str()).get(),
        commands::attributes::kCommand_Visibility_Local,
        command_manager_->GetCommandDictionary(), nullptr, nullptr);
    EXPECT_TRUE(!!command_instance);

    std::string id{base::IntToString(++command_id_)};
    command_instance->SetID(id);
    command_manager_->AddCommand(std::move(command_instance));
    EXPECT_EQ(CommandInstance::kStatusDone,
              command_manager_->FindCommand(id)->GetStatus());
  }

  std::shared_ptr<chromeos::http::fake::Transport> transport_;
  std::unique_ptr<DeviceRegistrationInfo> dev_reg_;
  std::shared_ptr<CommandManager> command_manager_;
  testing::NiceMock<MockStateChangeQueueInterface> mock_state_change_queue_;
  std::shared_ptr<StateManager> state_manager_;
  std::unique_ptr<BaseApiHandler> handler_;
  int command_id_{0};
};

TEST_F(BaseApiHandlerTest, UpdateBaseConfiguration) {
  LoadCommands(R"({
    'base': {
      'updateBaseConfiguration': {
        'parameters': {
          'localDiscoveryEnabled': 'boolean',
          'localAnonymousAccessMaxRole': [ 'none', 'viewer', 'user' ],
          'localPairingEnabled': 'boolean'
         },
         'results': {}
      }
    }
  })");

  const BuffetConfig& config{dev_reg_->GetConfig()};

  AddCommand(R"({
    'name' : 'base.updateBaseConfiguration',
    'parameters': {
      'localDiscoveryEnabled': false,
      'localAnonymousAccessMaxRole': 'none',
      'localPairingEnabled': false
    }
  })");
  EXPECT_EQ("none", config.local_anonymous_access_role());
  EXPECT_FALSE(config.local_discovery_enabled());
  EXPECT_FALSE(config.local_pairing_enabled());

  auto expected = R"({
    'base': {
      'firmwareVersion': '123123',
      'localAnonymousAccessMaxRole': 'none',
      'localDiscoveryEnabled': false,
      'localPairingEnabled': false,
      'network': {}
    }
  })";
  EXPECT_JSON_EQ(expected, *state_manager_->GetStateValuesAsJson(nullptr));

  AddCommand(R"({
    'name' : 'base.updateBaseConfiguration',
    'parameters': {
      'localDiscoveryEnabled': true,
      'localAnonymousAccessMaxRole': 'user',
      'localPairingEnabled': true
    }
  })");
  EXPECT_EQ("user", config.local_anonymous_access_role());
  EXPECT_TRUE(config.local_discovery_enabled());
  EXPECT_TRUE(config.local_pairing_enabled());
  expected = R"({
    'base': {
      'firmwareVersion': '123123',
      'localAnonymousAccessMaxRole': 'user',
      'localDiscoveryEnabled': true,
      'localPairingEnabled': true,
      'network': {}
    }
  })";
  EXPECT_JSON_EQ(expected, *state_manager_->GetStateValuesAsJson(nullptr));
}

TEST_F(BaseApiHandlerTest, UpdateDeviceInfo) {
  LoadCommands(R"({
    'base': {
      'updateDeviceInfo': {
        'parameters': {
          'description': 'string',
          'name': {
            'type': 'string',
            'minLength': 1
          },
          'location': 'string'
        },
        'results': {}
      }
    }
  })");

  AddCommand(R"({
    'name' : 'base.updateDeviceInfo',
    'parameters': {
      'name': 'testName',
      'description': 'testDescription',
      'location': 'testLocation'
    }
  })");

  const BuffetConfig& config{dev_reg_->GetConfig()};
  EXPECT_EQ("testName", config.name());
  EXPECT_EQ("testDescription", config.description());
  EXPECT_EQ("testLocation", config.location());
}

}  // namespace buffet
