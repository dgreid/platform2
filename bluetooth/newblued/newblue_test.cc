// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bluetooth/newblued/newblue.h"

#include <memory>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/test/task_environment.h>
#include <base/run_loop.h>
#include <base/stl_util.h>
#include <gtest/gtest.h>

#include "bluetooth/newblued/mock_libnewblue.h"

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::ElementsAre;
using ::testing::Pair;
using ::testing::Return;
using ::testing::SaveArg;

namespace bluetooth {

namespace {

constexpr uniq_t kDiscoveryHandle = 11;
// A random handle value.
constexpr uniq_t kPairStateChangeHandle = 3;
constexpr uniq_t kPasskeyDisplayObserverHandle = 4;
// Test scanning parameters
constexpr uint16_t kTestScanInterval = 36;  // 22.5 msec
constexpr uint16_t kTestScanWindow = 18;    // 11.25 msec

}  // namespace

class TestPairingAgent : public PairingAgent {
 public:
  void DisplayPasskey(const std::string& device_address,
                      uint32_t passkey) override {
    displayed_passkeys.push_back({device_address, passkey});
  }

  void RequestAuthorization(const std::string& device_address) override {}

  std::vector<std::pair<std::string, uint32_t>> displayed_passkeys;
};

class NewblueTest : public ::testing::Test {
 public:
  // A dummy struct that hosts the device information from discovery callback.
  struct MockDevice {
    std::string address;
    uint8_t address_type;
    std::string resolved_addr;
    int8_t rssi;
    uint8_t reply_type;
    std::vector<uint8_t> eir;
    PairState pair_state;
    PairError pair_error;
    std::string identity_address;
  };

  void SetUp() override {
    auto libnewblue = std::make_unique<MockLibNewblue>();
    libnewblue_ = libnewblue.get();
    newblue_ = std::make_unique<Newblue>(std::move(libnewblue));
  }

  bool StubHciUp(const uint8_t* address,
                 hciReadyForUpCbk callback,
                 void* callback_data) {
    callback(callback_data);
    return true;
  }

  void OnReadyForUp() { is_ready_for_up_ = true; }

  void OnDeviceDiscovered(const std::string& address,
                          uint8_t address_type,
                          const std::string& resolved_addr,
                          int8_t rssi,
                          uint8_t reply_type,
                          const std::vector<uint8_t>& eir) {
    discovered_devices_.push_back(
        {address, address_type, resolved_addr, rssi, reply_type, eir});
  }

  void OnPairStateChanged(const std::string& address,
                          PairState pair_state,
                          PairError pair_error,
                          const std::string& identity_address) {
    for (auto& dev : discovered_devices_) {
      if (dev.address == address) {
        dev.pair_state = pair_state;
        dev.pair_error = pair_error;
        dev.identity_address = identity_address;
      }
    }
  }

  void ExpectBringUp() {
    newblue_->Init();
    EXPECT_CALL(*libnewblue_, HciIsUp()).WillOnce(Return(false));
    EXPECT_FALSE(newblue_->BringUp());

    EXPECT_CALL(*libnewblue_, HciIsUp()).WillOnce(Return(true));
    EXPECT_CALL(*libnewblue_, L2cInit()).WillOnce(Return(0));
    EXPECT_CALL(*libnewblue_, AttInit()).WillOnce(Return(true));
    EXPECT_CALL(*libnewblue_, GattProfileInit()).WillOnce(Return(true));
    EXPECT_CALL(*libnewblue_, GattBuiltinInit()).WillOnce(Return(true));
    EXPECT_CALL(*libnewblue_, SmInit()).WillOnce(Return(true));
    EXPECT_CALL(*libnewblue_, SmRegisterPairStateObserver(_, _))
        .WillOnce(DoAll(SaveArg<0>(&pair_state_changed_callback_data_),
                        SaveArg<1>(&pair_state_changed_callback_),
                        Return(kPairStateChangeHandle)));
    EXPECT_CALL(*libnewblue_, SmRegisterPasskeyDisplayObserver(_, _))
        .WillOnce(DoAll(SaveArg<0>(&passkey_display_callback_data_),
                        SaveArg<1>(&passkey_display_callback_),
                        Return(kPasskeyDisplayObserverHandle)));
    EXPECT_TRUE(newblue_->BringUp());
  }

 protected:
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::ThreadingMode::MAIN_THREAD_ONLY};
  bool is_ready_for_up_ = false;
  std::unique_ptr<Newblue> newblue_;
  MockLibNewblue* libnewblue_;
  std::vector<MockDevice> discovered_devices_;
  smPairStateChangeCbk pair_state_changed_callback_;
  void* pair_state_changed_callback_data_ = nullptr;
  smPasskeyDisplayCbk passkey_display_callback_;
  void* passkey_display_callback_data_ = nullptr;
};

TEST_F(NewblueTest, ListenReadyForUp) {
  newblue_->Init();

  hciReadyForUpCbk up_callback;
  EXPECT_CALL(*libnewblue_, HciUp(_, _, _))
      .WillOnce(DoAll(SaveArg<1>(&up_callback),
                      Invoke(this, &NewblueTest::StubHciUp)));
  bool success = newblue_->ListenReadyForUp(
      base::Bind(&NewblueTest::OnReadyForUp, base::Unretained(this)));
  EXPECT_TRUE(success);
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(is_ready_for_up_);

  // If libnewblue says the stack is ready for up again, ignore that.
  // We shouldn't bring up the stack more than once.
  is_ready_for_up_ = false;
  up_callback(newblue_.get());
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(is_ready_for_up_);
}

TEST_F(NewblueTest, ListenReadyForUpFailed) {
  newblue_->Init();

  EXPECT_CALL(*libnewblue_, HciUp(_, _, _)).WillOnce(Return(false));
  bool success = newblue_->ListenReadyForUp(
      base::Bind(&NewblueTest::OnReadyForUp, base::Unretained(this)));
  EXPECT_FALSE(success);
}

TEST_F(NewblueTest, BringUp) {
  ExpectBringUp();
}

TEST_F(NewblueTest, StartDiscovery) {
  ExpectBringUp();

  hciDeviceDiscoveredLeCbk inquiry_response_callback;
  void* inquiry_response_callback_data;
  EXPECT_CALL(*libnewblue_,
              HciDiscoverLeStart(_, _, /* active */ true, kTestScanInterval,
                                 kTestScanWindow, /* useOwnRandomAddr */ false,
                                 /* onlyWhitelist */ false,
                                 /* filterDuplicates */ false))
      .WillOnce(DoAll(SaveArg<0>(&inquiry_response_callback),
                      SaveArg<1>(&inquiry_response_callback_data),
                      Return(kDiscoveryHandle)));
  newblue_->StartDiscovery(
      /* active */ true, kTestScanInterval, kTestScanWindow,
      /* use_random_addr */ false,
      /* only_whitelist */ false, /* filter_duplicates */ false,
      base::Bind(&NewblueTest::OnDeviceDiscovered, base::Unretained(this)));

  // 2 devices discovered.
  struct bt_addr addr1 = {.addr = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06},
                          .type = BT_ADDR_TYPE_LE_RANDOM};
  struct bt_addr resolved_addr1 = {
      .addr = {0x11, 0x12, 0x13, 0x14, 0x15, 0x16},
      .type = BT_ADDR_TYPE_LE_RANDOM};

  uint8_t eir1[] = {
      6, static_cast<uint8_t>(EirType::NAME_SHORT), 'a', 'l', 'i', 'c', 'e'};
  inquiry_response_callback(inquiry_response_callback_data, &addr1,
                            &resolved_addr1, -101, HCI_ADV_TYPE_SCAN_RSP, &eir1,
                            base::size(eir1));
  struct bt_addr addr2 = {.addr = {0x02, 0x03, 0x04, 0x05, 0x06, 0x07},
                          .type = BT_ADDR_TYPE_LE_PUBLIC};
  uint8_t eir2[] = {
      5, static_cast<uint8_t>(EirType::NAME_SHORT), 'b', 'o', 'b', '\0'};
  inquiry_response_callback(inquiry_response_callback_data, &addr2,
                            /* resolved_address */ nullptr, -102,
                            HCI_ADV_TYPE_ADV_IND, &eir2, base::size(eir2));
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(2, discovered_devices_.size());
  EXPECT_EQ("06:05:04:03:02:01", discovered_devices_[0].address);
  EXPECT_EQ("16:15:14:13:12:11", discovered_devices_[0].resolved_addr);
  EXPECT_EQ(BT_ADDR_TYPE_LE_RANDOM, discovered_devices_[0].address_type);
  EXPECT_EQ(-101, discovered_devices_[0].rssi);
  EXPECT_EQ(HCI_ADV_TYPE_SCAN_RSP, discovered_devices_[0].reply_type);
  EXPECT_EQ(std::vector<uint8_t>(eir1, eir1 + base::size(eir1)),
            discovered_devices_[0].eir);

  EXPECT_EQ("07:06:05:04:03:02", discovered_devices_[1].address);
  EXPECT_TRUE(discovered_devices_[1].resolved_addr.empty());
  EXPECT_EQ(-102, discovered_devices_[1].rssi);
  EXPECT_EQ(BT_ADDR_TYPE_LE_PUBLIC, discovered_devices_[1].address_type);
  EXPECT_EQ(-102, discovered_devices_[1].rssi);
  EXPECT_EQ(HCI_ADV_TYPE_ADV_IND, discovered_devices_[1].reply_type);
  EXPECT_EQ(std::vector<uint8_t>(eir2, eir2 + base::size(eir2)),
            discovered_devices_[1].eir);

  EXPECT_CALL(*libnewblue_, HciDiscoverLeStop(kDiscoveryHandle))
      .WillOnce(Return(true));
  newblue_->StopDiscovery();
  // Any inquiry response after StopDiscovery should be ignored.
  inquiry_response_callback(inquiry_response_callback_data, &addr1,
                            /* resolved_address */ nullptr, -101,
                            HCI_ADV_TYPE_SCAN_RSP, &eir1, base::size(eir1));
  base::RunLoop().RunUntilIdle();
  // Check that discovered_devices_ is still the same.
  EXPECT_EQ(2, discovered_devices_.size());
}

TEST_F(NewblueTest, PairStateChanged) {
  ExpectBringUp();

  hciDeviceDiscoveredLeCbk inquiry_response_callback;
  void* inquiry_response_callback_data;
  EXPECT_CALL(*libnewblue_,
              HciDiscoverLeStart(_, _, /* active */ true, kTestScanInterval,
                                 kTestScanWindow, /* useOwnRandomAddr */ false,
                                 /* onlyWhitelist */ false,
                                 /* filterDuplicates */ false))
      .WillOnce(DoAll(SaveArg<0>(&inquiry_response_callback),
                      SaveArg<1>(&inquiry_response_callback_data),
                      Return(kDiscoveryHandle)));
  newblue_->StartDiscovery(
      /* active */ true, kTestScanInterval, kTestScanWindow,
      /* use_random_addr */ false,
      /* only_whitelist */ false, /* filter_duplicates */ false,
      base::Bind(&NewblueTest::OnDeviceDiscovered, base::Unretained(this)));

  // 1 device discovered.
  struct bt_addr addr1 = {.addr = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06},
                          .type = BT_ADDR_TYPE_LE_RANDOM};
  struct bt_addr identity_addr = {.addr = {0x11, 0x12, 0x13, 0x14, 0x15, 0x16},
                                  .type = BT_ADDR_TYPE_LE_RANDOM};
  uint8_t eir1[] = {
      6, static_cast<uint8_t>(EirType::NAME_SHORT), 'a', 'l', 'i', 'c', 'e'};
  inquiry_response_callback(inquiry_response_callback_data, &addr1,
                            /* resolved_address */ nullptr, -101,
                            HCI_ADV_TYPE_SCAN_RSP, &eir1, base::size(eir1));
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(1, discovered_devices_.size());
  EXPECT_EQ("06:05:04:03:02:01", discovered_devices_[0].address);
  EXPECT_EQ(-101, discovered_devices_[0].rssi);
  EXPECT_EQ(PairState::NOT_PAIRED, discovered_devices_[0].pair_state);

  // Register as a pairing state observer.
  UniqueId pair_observer_handle = newblue_->RegisterAsPairObserver(
      base::Bind(&NewblueTest::OnPairStateChanged, base::Unretained(this)));
  EXPECT_NE(kInvalidUniqueId, pair_observer_handle);

  // Pairing started.
  struct smPairStateChange state_change = {.pairState = SM_PAIR_STATE_START,
                                           .pairErr = SM_PAIR_ERR_NONE,
                                           .peerAddr = addr1};
  pair_state_changed_callback_(pair_state_changed_callback_data_, &state_change,
                               kPairStateChangeHandle);
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(1, discovered_devices_.size());
  EXPECT_EQ("06:05:04:03:02:01", discovered_devices_[0].address);
  EXPECT_EQ(-101, discovered_devices_[0].rssi);
  EXPECT_EQ(PairState::STARTED, discovered_devices_[0].pair_state);
  EXPECT_EQ(PairError::NONE, discovered_devices_[0].pair_error);

  // Pairing failed with SM_PAIR_ERR_L2C_CONN error.
  state_change.pairState = SM_PAIR_STATE_FAILED;
  state_change.pairErr = SM_PAIR_ERR_L2C_CONN;

  pair_state_changed_callback_(pair_state_changed_callback_data_, &state_change,
                               kPairStateChangeHandle);
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(1, discovered_devices_.size());
  EXPECT_EQ("06:05:04:03:02:01", discovered_devices_[0].address);
  EXPECT_EQ(-101, discovered_devices_[0].rssi);
  EXPECT_EQ(PairState::FAILED, discovered_devices_[0].pair_state);
  EXPECT_EQ(PairError::L2C_CONN, discovered_devices_[0].pair_error);

  // Pairing succeeded with identity address set
  state_change.pairState = SM_PAIR_STATE_PAIRED;
  state_change.pairErr = SM_PAIR_ERR_NONE;
  state_change.peerIdentityAddr = identity_addr;

  pair_state_changed_callback_(pair_state_changed_callback_data_, &state_change,
                               kPairStateChangeHandle);
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(1, discovered_devices_.size());
  EXPECT_EQ("06:05:04:03:02:01", discovered_devices_[0].address);
  EXPECT_EQ(PairState::PAIRED, discovered_devices_[0].pair_state);
  EXPECT_EQ(PairError::NONE, discovered_devices_[0].pair_error);
  EXPECT_EQ("16:15:14:13:12:11", discovered_devices_[0].identity_address);
}

TEST_F(NewblueTest, Pair) {
  ExpectBringUp();

  hciDeviceDiscoveredLeCbk inquiry_response_callback;
  void* inquiry_response_callback_data;
  EXPECT_CALL(*libnewblue_,
              HciDiscoverLeStart(_, _, /* active */ true, kTestScanInterval,
                                 kTestScanWindow, /* useOwnRandomAddr */ false,
                                 /* onlyWhitelist */ false,
                                 /* filterDuplicates */ false))
      .WillOnce(DoAll(SaveArg<0>(&inquiry_response_callback),
                      SaveArg<1>(&inquiry_response_callback_data),
                      Return(kDiscoveryHandle)));
  newblue_->StartDiscovery(
      /* active */ true, kTestScanInterval, kTestScanWindow,
      /* use_random_addr */ false,
      /* only_whitelist */ false, /* filter_duplicates */ false,
      base::Bind(&NewblueTest::OnDeviceDiscovered, base::Unretained(this)));

  // 1 device discovered.
  struct bt_addr addr = {.addr = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06},
                         .type = BT_ADDR_TYPE_LE_RANDOM};
  std::string device_addr("06:05:04:03:02:01");
  uint8_t eir[] = {
      // Flag
      3, static_cast<uint8_t>(EirType::FLAGS), 0xAA, 0xBB,
      // Name
      6, static_cast<uint8_t>(EirType::NAME_SHORT), 'm', 'o', 'u', 's', 'e',
      // Appearance
      3, static_cast<uint8_t>(EirType::GAP_APPEARANCE), 0xc2, 0x03};

  inquiry_response_callback(inquiry_response_callback_data, &addr,
                            /* resolved_address */ nullptr, -101,
                            HCI_ADV_TYPE_SCAN_RSP, &eir, base::size(eir));
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(1, discovered_devices_.size());
  EXPECT_EQ(device_addr, discovered_devices_[0].address);
  EXPECT_EQ(-101, discovered_devices_[0].rssi);
  EXPECT_EQ(PairState::NOT_PAIRED, discovered_devices_[0].pair_state);

  EXPECT_CALL(*libnewblue_, SmPair(_, _)).WillOnce(Return());

  EXPECT_TRUE(
      newblue_->Pair(device_addr, true, {.bond = false, .mitm = false}));
  base::RunLoop().RunUntilIdle();
}

TEST_F(NewblueTest, CancelPairing) {
  ExpectBringUp();

  hciDeviceDiscoveredLeCbk inquiry_response_callback;
  void* inquiry_response_callback_data;
  EXPECT_CALL(*libnewblue_,
              HciDiscoverLeStart(_, _, /* active */ true, kTestScanInterval,
                                 kTestScanWindow, /* useOwnRandomAddr */ false,
                                 /* onlyWhitelist */ false,
                                 /* filterDuplicates */ false))
      .WillOnce(DoAll(SaveArg<0>(&inquiry_response_callback),
                      SaveArg<1>(&inquiry_response_callback_data),
                      Return(kDiscoveryHandle)));
  newblue_->StartDiscovery(
      /* active */ true, kTestScanInterval, kTestScanWindow,
      /* use_random_addr */ false,
      /* only_whitelist */ false, /* filter_duplicates */ false,
      base::Bind(&NewblueTest::OnDeviceDiscovered, base::Unretained(this)));

  // 1 device discovered.
  struct bt_addr addr = {.addr = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06},
                         .type = BT_ADDR_TYPE_LE_RANDOM};
  std::string device_addr("06:05:04:03:02:01");
  uint8_t eir[] = {
      // Flag
      3, static_cast<uint8_t>(EirType::FLAGS), 0xAA, 0xBB,
      // Name
      6, static_cast<uint8_t>(EirType::NAME_SHORT), 'm', 'o', 'u', 's', 'e',
      // Appearance
      3, static_cast<uint8_t>(EirType::GAP_APPEARANCE), 0xc2, 0x03};

  inquiry_response_callback(inquiry_response_callback_data, &addr,
                            /* resolved_address */ nullptr, -101,
                            HCI_ADV_TYPE_SCAN_RSP, &eir, base::size(eir));
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(1, discovered_devices_.size());
  EXPECT_EQ(device_addr, discovered_devices_[0].address);
  EXPECT_EQ(-101, discovered_devices_[0].rssi);
  EXPECT_EQ(PairState::NOT_PAIRED, discovered_devices_[0].pair_state);

  // Register as a pairing state observer.
  UniqueId pair_observer_handle = newblue_->RegisterAsPairObserver(
      base::Bind(&NewblueTest::OnPairStateChanged, base::Unretained(this)));
  EXPECT_NE(kInvalidUniqueId, pair_observer_handle);

  EXPECT_CALL(*libnewblue_, SmPair(_, _)).WillOnce(Return());
  EXPECT_TRUE(
      newblue_->Pair(device_addr, true, {.bond = false, .mitm = false}));
  base::RunLoop().RunUntilIdle();

  // Pairing started.
  struct smPairStateChange state_change = {.pairState = SM_PAIR_STATE_START,
                                           .pairErr = SM_PAIR_ERR_NONE,
                                           .peerAddr = addr};
  pair_state_changed_callback_(pair_state_changed_callback_data_, &state_change,
                               kPairStateChangeHandle);
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(1, discovered_devices_.size());
  EXPECT_EQ("06:05:04:03:02:01", discovered_devices_[0].address);
  EXPECT_EQ(-101, discovered_devices_[0].rssi);
  EXPECT_EQ(PairState::STARTED, discovered_devices_[0].pair_state);

  // Cancel pairing.
  EXPECT_CALL(*libnewblue_, SmUnpair(_)).WillOnce(Return());
  EXPECT_TRUE(newblue_->CancelPair(device_addr, true));
  base::RunLoop().RunUntilIdle();
}

TEST_F(NewblueTest, PasskeyDisplayObserver) {
  ExpectBringUp();

  TestPairingAgent pairing_agent;
  newblue_->RegisterPairingAgent(&pairing_agent);

  struct bt_addr peer_addr = {.addr = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06},
                              .type = BT_ADDR_TYPE_LE_RANDOM};
  struct smPasskeyDisplay passkey_display = {
      .valid = true, .passkey = 123456, .peerAddr = peer_addr};
  passkey_display_callback_(passkey_display_callback_data_, &passkey_display,
                            kPasskeyDisplayObserverHandle);
  base::RunLoop().RunUntilIdle();
  EXPECT_THAT(pairing_agent.displayed_passkeys,
              ElementsAre(Pair("06:05:04:03:02:01", 123456)));
}

}  // namespace bluetooth
