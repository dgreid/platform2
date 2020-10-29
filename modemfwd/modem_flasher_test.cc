// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modemfwd/modem_flasher.h"

#include <memory>
#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <gtest/gtest.h>

#include "modemfwd/firmware_directory_stub.h"
#include "modemfwd/mock_journal.h"
#include "modemfwd/mock_modem.h"

using ::testing::_;
using ::testing::AtLeast;
using ::testing::Mock;
using ::testing::Return;

namespace modemfwd {

namespace {

constexpr char kDeviceId1[] = "device:id:1";
constexpr char kEquipmentId1[] = "equipment_id_1";

constexpr char kMainFirmware1Path[] = "main_fw_1.fls";
constexpr char kMainFirmware1Version[] = "versionA";

constexpr char kMainFirmware2Path[] = "main_fw_2.fls";
constexpr char kMainFirmware2Version[] = "versionB";

constexpr char kCarrier1[] = "uuid_1";
constexpr char kCarrier1Mvno[] = "uuid_1_1";
constexpr char kCarrier1Firmware1Path[] = "carrier_1_fw_1.fls";
constexpr char kCarrier1Firmware1Version[] = "v1.00";
constexpr char kCarrier1Firmware2Path[] = "carrier_1_fw_2.fls";
constexpr char kCarrier1Firmware2Version[] = "v1.10";

constexpr char kCarrier2[] = "uuid_2";
constexpr char kCarrier2Firmware1Path[] = "carrier_2_fw_1.fls";
constexpr char kCarrier2Firmware1Version[] = "4500.15.65";

constexpr char kGenericCarrierFirmware1Path[] = "generic_fw_1.fls";
constexpr char kGenericCarrierFirmware1Version[] = "2017-10-13";
constexpr char kGenericCarrierFirmware2Path[] = "generic_fw_2.fls";
constexpr char kGenericCarrierFirmware2Version[] = "2017-10-14";

}  // namespace

class ModemFlasherTest : public ::testing::Test {
 public:
  ModemFlasherTest() {
    auto firmware_directory = std::make_unique<FirmwareDirectoryStub>();
    firmware_directory_ = firmware_directory.get();

    auto journal = std::make_unique<MockJournal>();
    journal_ = journal.get();

    modem_flasher_ = std::make_unique<ModemFlasher>(
        std::move(firmware_directory), std::move(journal));
  }

 protected:
  void AddMainFirmwareFile(const std::string& device_id,
                           const base::FilePath& firmware_path,
                           const std::string& version) {
    FirmwareFileInfo firmware_info(firmware_path, version);
    firmware_directory_->AddMainFirmware(kDeviceId1, firmware_info);
  }

  void AddMainFirmwareFileForCarrier(const std::string& device_id,
                                     const std::string& carrier_name,
                                     const base::FilePath& firmware_path,
                                     const std::string& version) {
    FirmwareFileInfo firmware_info(firmware_path, version);
    firmware_directory_->AddMainFirmwareForCarrier(kDeviceId1, carrier_name,
                                                   firmware_info);
  }

  void AddCarrierFirmwareFile(const std::string& device_id,
                              const std::string& carrier_name,
                              const base::FilePath& firmware_path,
                              const std::string& version) {
    FirmwareFileInfo firmware_info(firmware_path, version);
    firmware_directory_->AddCarrierFirmware(kDeviceId1, carrier_name,
                                            firmware_info);
  }

  std::unique_ptr<MockModem> GetDefaultModem() {
    auto modem = std::make_unique<MockModem>();
    ON_CALL(*modem, GetDeviceId()).WillByDefault(Return(kDeviceId1));
    ON_CALL(*modem, GetEquipmentId()).WillByDefault(Return(kEquipmentId1));
    ON_CALL(*modem, GetCarrierId()).WillByDefault(Return(kCarrier1));
    ON_CALL(*modem, GetMainFirmwareVersion())
        .WillByDefault(Return(kMainFirmware1Version));
    ON_CALL(*modem, GetCarrierFirmwareId()).WillByDefault(Return(""));
    ON_CALL(*modem, GetCarrierFirmwareVersion()).WillByDefault(Return(""));

    // Since the equipment ID is the main identifier we should always expect
    // to want to know what it is.
    EXPECT_CALL(*modem, GetEquipmentId()).Times(AtLeast(1));
    return modem;
  }

  void SetCarrierFirmwareInfo(MockModem* modem,
                              const std::string& carrier_id,
                              const std::string& version) {
    ON_CALL(*modem, GetCarrierFirmwareId()).WillByDefault(Return(carrier_id));
    ON_CALL(*modem, GetCarrierFirmwareVersion()).WillByDefault(Return(version));
  }

  MockJournal* journal_;
  std::unique_ptr<ModemFlasher> modem_flasher_;

 private:
  // We pass this off to |modem_flasher_| but keep a reference to it to ensure
  // we can set up the stub outputs.
  FirmwareDirectoryStub* firmware_directory_;
};

TEST_F(ModemFlasherTest, NothingToFlash) {
  auto modem = GetDefaultModem();
  EXPECT_CALL(*modem, GetDeviceId()).Times(AtLeast(1));
  EXPECT_CALL(*modem, FlashMainFirmware(_, _)).Times(0);
  EXPECT_CALL(*modem, FlashCarrierFirmware(_, _)).Times(0);
  modem_flasher_->TryFlash(modem.get());
}

TEST_F(ModemFlasherTest, FlashMainFirmware) {
  base::FilePath new_firmware(kMainFirmware2Path);
  AddMainFirmwareFile(kDeviceId1, new_firmware, kMainFirmware2Version);

  auto modem = GetDefaultModem();
  EXPECT_CALL(*modem, GetDeviceId()).Times(AtLeast(1));
  EXPECT_CALL(*modem, GetMainFirmwareVersion()).Times(AtLeast(1));
  EXPECT_CALL(*modem, FlashMainFirmware(new_firmware, kMainFirmware2Version))
      .WillOnce(Return(true));
  EXPECT_CALL(*modem, FlashCarrierFirmware(_, _)).Times(0);
  modem_flasher_->TryFlash(modem.get());
}

TEST_F(ModemFlasherTest, SkipSameMainVersion) {
  base::FilePath firmware(kMainFirmware1Path);
  AddMainFirmwareFile(kDeviceId1, firmware, kMainFirmware1Version);

  auto modem = GetDefaultModem();
  EXPECT_CALL(*modem, GetDeviceId()).Times(AtLeast(1));
  EXPECT_CALL(*modem, GetMainFirmwareVersion()).Times(AtLeast(1));
  EXPECT_CALL(*modem, FlashMainFirmware(_, _)).Times(0);
  EXPECT_CALL(*modem, FlashCarrierFirmware(_, _)).Times(0);
  modem_flasher_->TryFlash(modem.get());
}

TEST_F(ModemFlasherTest, UpgradeCarrierFirmware) {
  base::FilePath new_firmware(kCarrier1Firmware2Path);
  AddCarrierFirmwareFile(kDeviceId1, kCarrier1, new_firmware,
                         kCarrier1Firmware2Version);

  auto modem = GetDefaultModem();
  EXPECT_CALL(*modem, GetDeviceId()).Times(AtLeast(1));
  SetCarrierFirmwareInfo(modem.get(), kCarrier1, kCarrier1Firmware1Version);
  EXPECT_CALL(*modem, FlashMainFirmware(_, _)).Times(0);
  EXPECT_CALL(*modem,
              FlashCarrierFirmware(new_firmware, kCarrier1Firmware2Version))
      .WillOnce(Return(true));
  modem_flasher_->TryFlash(modem.get());
}

TEST_F(ModemFlasherTest, SwitchCarrierFirmwareForSimHotSwap) {
  base::FilePath original_firmware(kCarrier1Firmware1Path);
  base::FilePath other_firmware(kCarrier2Firmware1Path);
  AddCarrierFirmwareFile(kDeviceId1, kCarrier1, original_firmware,
                         kCarrier1Firmware1Version);
  AddCarrierFirmwareFile(kDeviceId1, kCarrier2, other_firmware,
                         kCarrier2Firmware1Version);

  auto modem = GetDefaultModem();
  EXPECT_CALL(*modem, GetDeviceId()).Times(AtLeast(1));
  EXPECT_CALL(*modem, GetCarrierId())
      .Times(AtLeast(1))
      .WillRepeatedly(Return(kCarrier2));
  SetCarrierFirmwareInfo(modem.get(), kCarrier1, kCarrier1Firmware1Version);
  EXPECT_CALL(*modem, FlashMainFirmware(_, _)).Times(0);
  EXPECT_CALL(*modem,
              FlashCarrierFirmware(other_firmware, kCarrier2Firmware1Version))
      .WillOnce(Return(true));
  modem_flasher_->TryFlash(modem.get());

  // After the modem reboots, the helper hopefully reports the new carrier.
  SetCarrierFirmwareInfo(modem.get(), kCarrier2, kCarrier2Firmware1Version);
  EXPECT_CALL(*modem, FlashMainFirmware(_, _)).Times(0);
  EXPECT_CALL(*modem, FlashCarrierFirmware(_, _)).Times(0);
  modem_flasher_->TryFlash(modem.get());

  // Suppose we swap the SIM back to the first one. Then we should try to
  // flash the first firmware again.
  modem = GetDefaultModem();
  EXPECT_CALL(*modem, GetDeviceId()).Times(AtLeast(1));
  EXPECT_CALL(*modem, GetCarrierId()).Times(AtLeast(1));
  SetCarrierFirmwareInfo(modem.get(), kCarrier2, kCarrier2Firmware1Version);
  EXPECT_CALL(*modem, FlashMainFirmware(_, _)).Times(0);
  EXPECT_CALL(*modem, FlashCarrierFirmware(original_firmware,
                                           kCarrier1Firmware1Version))
      .WillOnce(Return(true));
  modem_flasher_->TryFlash(modem.get());
}

TEST_F(ModemFlasherTest, BlockAfterMainFlashFailure) {
  base::FilePath new_firmware(kMainFirmware2Path);
  AddMainFirmwareFile(kDeviceId1, new_firmware, kMainFirmware2Version);

  auto modem = GetDefaultModem();
  EXPECT_CALL(*modem, GetDeviceId()).Times(AtLeast(1));
  EXPECT_CALL(*modem, GetMainFirmwareVersion()).Times(AtLeast(1));
  EXPECT_CALL(*modem, FlashMainFirmware(new_firmware, kMainFirmware2Version))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(*modem, FlashCarrierFirmware(_, _)).Times(0);
  modem_flasher_->TryFlash(modem.get());

  // ModemFlasher retries once on a failure, so fail twice.
  modem = GetDefaultModem();
  modem_flasher_->TryFlash(modem.get());

  // Here the modem would reboot, but ModemFlasher should keep track of its
  // IMEI and ensure we don't even check the main firmware version or
  // carrier.
  modem = GetDefaultModem();
  EXPECT_CALL(*modem, GetDeviceId()).Times(0);
  EXPECT_CALL(*modem, GetMainFirmwareVersion()).Times(0);
  EXPECT_CALL(*modem, GetCarrierId()).Times(0);
  EXPECT_CALL(*modem, FlashMainFirmware(_, _)).Times(0);
  EXPECT_CALL(*modem, FlashCarrierFirmware(_, _)).Times(0);
  modem_flasher_->TryFlash(modem.get());
}

TEST_F(ModemFlasherTest, BlockAfterCarrierFlashFailure) {
  base::FilePath new_firmware(kCarrier1Firmware2Path);
  AddCarrierFirmwareFile(kDeviceId1, kCarrier1, new_firmware,
                         kCarrier1Firmware2Version);

  auto modem = GetDefaultModem();
  EXPECT_CALL(*modem, GetDeviceId()).Times(AtLeast(1));
  SetCarrierFirmwareInfo(modem.get(), kCarrier1, kCarrier1Firmware1Version);
  EXPECT_CALL(*modem, FlashMainFirmware(_, _)).Times(0);
  EXPECT_CALL(*modem,
              FlashCarrierFirmware(new_firmware, kCarrier1Firmware2Version))
      .WillRepeatedly(Return(false));
  modem_flasher_->TryFlash(modem.get());

  // ModemFlasher retries once on a failure, so fail twice.
  modem = GetDefaultModem();
  modem_flasher_->TryFlash(modem.get());

  modem = GetDefaultModem();
  EXPECT_CALL(*modem, GetDeviceId()).Times(0);
  EXPECT_CALL(*modem, GetMainFirmwareVersion()).Times(0);
  EXPECT_CALL(*modem, GetCarrierId()).Times(0);
  EXPECT_CALL(*modem, FlashMainFirmware(_, _)).Times(0);
  EXPECT_CALL(*modem, FlashCarrierFirmware(_, _)).Times(0);
  modem_flasher_->TryFlash(modem.get());
}

TEST_F(ModemFlasherTest, SuccessThenBlock) {
  const base::FilePath new_main_firmware(kMainFirmware2Path);
  const base::FilePath new_carrier_firmware(kCarrier1Firmware2Path);
  AddMainFirmwareFile(kDeviceId1, new_main_firmware, kMainFirmware2Version);
  AddCarrierFirmwareFile(kDeviceId1, kCarrier1, new_carrier_firmware,
                         kCarrier1Firmware2Version);

  // Successfully flash the main firmware.
  auto modem = GetDefaultModem();
  EXPECT_CALL(*modem, GetDeviceId()).Times(AtLeast(1));
  EXPECT_CALL(*modem, GetMainFirmwareVersion()).Times(AtLeast(1));
  SetCarrierFirmwareInfo(modem.get(), kCarrier1, kCarrier1Firmware1Version);
  EXPECT_CALL(*modem,
              FlashMainFirmware(new_main_firmware, kMainFirmware2Version))
      .WillOnce(Return(true));
  EXPECT_CALL(*modem, FlashCarrierFirmware(_, _)).Times(0);
  modem_flasher_->TryFlash(modem.get());

  // Fail to flash the carrier firmware.
  modem = GetDefaultModem();
  EXPECT_CALL(*modem, GetDeviceId()).Times(AtLeast(1));
  SetCarrierFirmwareInfo(modem.get(), kCarrier1, kCarrier1Firmware1Version);
  EXPECT_CALL(*modem, FlashMainFirmware(_, _)).Times(0);
  EXPECT_CALL(*modem, FlashCarrierFirmware(new_carrier_firmware,
                                           kCarrier1Firmware2Version))
      .WillRepeatedly(Return(false));
  modem_flasher_->TryFlash(modem.get());

  // ModemFlasher retries once on a failure, so fail twice.
  modem = GetDefaultModem();
  modem_flasher_->TryFlash(modem.get());

  // Check that we're still blocked even though we saw a successful flash
  // earlier.
  modem = GetDefaultModem();
  EXPECT_CALL(*modem, GetDeviceId()).Times(0);
  EXPECT_CALL(*modem, GetMainFirmwareVersion()).Times(0);
  EXPECT_CALL(*modem, GetCarrierId()).Times(0);
  SetCarrierFirmwareInfo(modem.get(), kCarrier1, kCarrier1Firmware1Version);
  EXPECT_CALL(*modem, FlashMainFirmware(_, _)).Times(0);
  EXPECT_CALL(*modem, FlashCarrierFirmware(_, _)).Times(0);
  modem_flasher_->TryFlash(modem.get());
}

TEST_F(ModemFlasherTest, RefuseToFlashMainFirmwareTwice) {
  base::FilePath new_firmware(kMainFirmware2Path);
  AddMainFirmwareFile(kDeviceId1, new_firmware, kMainFirmware2Version);

  auto modem = GetDefaultModem();
  EXPECT_CALL(*modem, GetDeviceId()).Times(AtLeast(1));
  EXPECT_CALL(*modem, GetMainFirmwareVersion()).Times(AtLeast(1));
  EXPECT_CALL(*modem, FlashMainFirmware(new_firmware, kMainFirmware2Version))
      .WillOnce(Return(true));
  EXPECT_CALL(*modem, FlashCarrierFirmware(_, _)).Times(0);
  modem_flasher_->TryFlash(modem.get());

  // We've had issues in the past where the firmware version is updated
  // but the modem still reports the old version string. Refuse to flash
  // the main firmware twice because that should never be correct behavior
  // in one session. Otherwise, we might try to flash the main firmware
  // over and over.
  modem = GetDefaultModem();
  EXPECT_CALL(*modem, GetDeviceId()).Times(AtLeast(1));
  EXPECT_CALL(*modem, GetMainFirmwareVersion()).Times(0);
  EXPECT_CALL(*modem, FlashMainFirmware(_, _)).Times(0);
  EXPECT_CALL(*modem, FlashCarrierFirmware(_, _)).Times(0);
  modem_flasher_->TryFlash(modem.get());
}

TEST_F(ModemFlasherTest, RefuseToFlashCarrierFirmwareTwice) {
  base::FilePath new_firmware(kCarrier1Firmware2Path);
  AddCarrierFirmwareFile(kDeviceId1, kCarrier1, new_firmware,
                         kCarrier1Firmware2Version);

  auto modem = GetDefaultModem();
  EXPECT_CALL(*modem, GetDeviceId()).Times(AtLeast(1));
  SetCarrierFirmwareInfo(modem.get(), kCarrier1, kCarrier1Firmware1Version);
  EXPECT_CALL(*modem, FlashMainFirmware(_, _)).Times(0);
  EXPECT_CALL(*modem,
              FlashCarrierFirmware(new_firmware, kCarrier1Firmware2Version))
      .WillOnce(Return(true));
  modem_flasher_->TryFlash(modem.get());

  // Assume the carrier firmware doesn't have an updated version string in it,
  // i.e. the modem will return the old version string even if it's been
  // updated.
  modem = GetDefaultModem();
  EXPECT_CALL(*modem, GetDeviceId()).Times(AtLeast(1));
  SetCarrierFirmwareInfo(modem.get(), kCarrier1, kCarrier1Firmware1Version);
  EXPECT_CALL(*modem, FlashMainFirmware(_, _)).Times(0);
  EXPECT_CALL(*modem, FlashCarrierFirmware(_, _)).Times(0);
  modem_flasher_->TryFlash(modem.get());
}

TEST_F(ModemFlasherTest, RefuseToReflashCarrierAcrossHotSwap) {
  // Upgrade carrier firmware.
  base::FilePath new_firmware(kCarrier1Firmware2Path);
  AddCarrierFirmwareFile(kDeviceId1, kCarrier1, new_firmware,
                         kCarrier1Firmware2Version);

  auto modem = GetDefaultModem();
  EXPECT_CALL(*modem, GetDeviceId()).Times(AtLeast(1));
  EXPECT_CALL(*modem, GetCarrierId()).Times(AtLeast(1));
  SetCarrierFirmwareInfo(modem.get(), kCarrier1, kCarrier1Firmware1Version);
  EXPECT_CALL(*modem, FlashMainFirmware(_, _)).Times(0);
  EXPECT_CALL(*modem,
              FlashCarrierFirmware(new_firmware, kCarrier1Firmware2Version))
      .WillOnce(Return(true));
  modem_flasher_->TryFlash(modem.get());

  // Switch carriers, but there won't be firmware for the new one.
  modem = GetDefaultModem();
  EXPECT_CALL(*modem, GetDeviceId()).Times(AtLeast(1));
  EXPECT_CALL(*modem, GetCarrierId())
      .Times(AtLeast(1))
      .WillRepeatedly(Return(kCarrier2));
  SetCarrierFirmwareInfo(modem.get(), kCarrier1, kCarrier1Firmware2Version);
  EXPECT_CALL(*modem, FlashMainFirmware(_, _)).Times(0);
  EXPECT_CALL(*modem, FlashCarrierFirmware(_, _)).Times(0);
  modem_flasher_->TryFlash(modem.get());

  // Suppose we swap the SIM back to the first one. We should not flash
  // firmware that we already know we successfully flashed.
  modem = GetDefaultModem();
  EXPECT_CALL(*modem, GetDeviceId()).Times(AtLeast(1));
  EXPECT_CALL(*modem, GetCarrierId()).Times(AtLeast(1));
  SetCarrierFirmwareInfo(modem.get(), kCarrier1, kCarrier1Firmware2Version);
  EXPECT_CALL(*modem, FlashMainFirmware(_, _)).Times(0);
  EXPECT_CALL(*modem, FlashCarrierFirmware(_, _)).Times(0);
  modem_flasher_->TryFlash(modem.get());
}

TEST_F(ModemFlasherTest, UpgradeGenericFirmware) {
  base::FilePath new_firmware(kGenericCarrierFirmware2Path);
  AddCarrierFirmwareFile(kDeviceId1, FirmwareDirectory::kGenericCarrierId,
                         new_firmware, kGenericCarrierFirmware2Version);

  auto modem = GetDefaultModem();
  EXPECT_CALL(*modem, GetDeviceId()).Times(AtLeast(1));
  EXPECT_CALL(*modem, GetCarrierId()).Times(AtLeast(1));
  SetCarrierFirmwareInfo(modem.get(), FirmwareDirectory::kGenericCarrierId,
                         kGenericCarrierFirmware1Version);
  EXPECT_CALL(*modem, FlashMainFirmware(_, _)).Times(0);
  EXPECT_CALL(*modem, FlashCarrierFirmware(new_firmware,
                                           kGenericCarrierFirmware2Version))
      .WillOnce(Return(true));
  modem_flasher_->TryFlash(modem.get());
}

TEST_F(ModemFlasherTest, SkipSameGenericFirmware) {
  base::FilePath generic_firmware(kGenericCarrierFirmware1Path);
  AddCarrierFirmwareFile(kDeviceId1, FirmwareDirectory::kGenericCarrierId,
                         generic_firmware, kGenericCarrierFirmware1Version);

  auto modem = GetDefaultModem();
  EXPECT_CALL(*modem, GetDeviceId()).Times(AtLeast(1));
  EXPECT_CALL(*modem, GetCarrierId()).Times(AtLeast(1));
  SetCarrierFirmwareInfo(modem.get(), FirmwareDirectory::kGenericCarrierId,
                         kGenericCarrierFirmware1Version);
  EXPECT_CALL(*modem, FlashMainFirmware(_, _)).Times(0);
  EXPECT_CALL(*modem, FlashCarrierFirmware(_, _)).Times(0);
  modem_flasher_->TryFlash(modem.get());
}

TEST_F(ModemFlasherTest, TwoCarriersUsingGenericFirmware) {
  base::FilePath generic_firmware(kGenericCarrierFirmware1Path);
  AddCarrierFirmwareFile(kDeviceId1, FirmwareDirectory::kGenericCarrierId,
                         generic_firmware, kGenericCarrierFirmware1Version);

  auto modem = GetDefaultModem();
  EXPECT_CALL(*modem, GetDeviceId()).Times(AtLeast(1));
  EXPECT_CALL(*modem, GetCarrierId()).Times(AtLeast(1));
  EXPECT_CALL(*modem, FlashMainFirmware(_, _)).Times(0);
  EXPECT_CALL(*modem, FlashCarrierFirmware(generic_firmware,
                                           kGenericCarrierFirmware1Version))
      .WillOnce(Return(true));
  modem_flasher_->TryFlash(modem.get());

  // When we try to flash again and the modem reports a different carrier,
  // we should expect that the ModemFlasher refuses to flash the same firmware,
  // since there is generic firmware and no carrier has its own firmware.
  modem = GetDefaultModem();
  EXPECT_CALL(*modem, GetDeviceId()).Times(AtLeast(1));
  EXPECT_CALL(*modem, GetCarrierId()).Times(AtLeast(1));
  SetCarrierFirmwareInfo(modem.get(), FirmwareDirectory::kGenericCarrierId,
                         kGenericCarrierFirmware1Version);
  EXPECT_CALL(*modem, FlashMainFirmware(_, _)).Times(0);
  EXPECT_CALL(*modem, FlashCarrierFirmware(_, _)).Times(0);
  modem_flasher_->TryFlash(modem.get());
}

TEST_F(ModemFlasherTest, HotSwapWithGenericFirmware) {
  base::FilePath original_firmware(kGenericCarrierFirmware1Path);
  base::FilePath other_firmware(kCarrier2Firmware1Path);
  AddCarrierFirmwareFile(kDeviceId1, FirmwareDirectory::kGenericCarrierId,
                         original_firmware, kGenericCarrierFirmware1Version);
  AddCarrierFirmwareFile(kDeviceId1, kCarrier2, other_firmware,
                         kCarrier2Firmware1Version);

  // Even though there is generic firmware, we should try to use specific
  // ones first if they exist.
  auto modem = GetDefaultModem();
  EXPECT_CALL(*modem, GetDeviceId()).Times(AtLeast(1));
  EXPECT_CALL(*modem, GetCarrierId())
      .Times(AtLeast(1))
      .WillRepeatedly(Return(kCarrier2));
  SetCarrierFirmwareInfo(modem.get(), FirmwareDirectory::kGenericCarrierId,
                         kGenericCarrierFirmware1Version);
  EXPECT_CALL(*modem, FlashMainFirmware(_, _)).Times(0);
  EXPECT_CALL(*modem,
              FlashCarrierFirmware(other_firmware, kCarrier2Firmware1Version))
      .WillOnce(Return(true));
  modem_flasher_->TryFlash(modem.get());

  // Reboot the modem.
  SetCarrierFirmwareInfo(modem.get(), kCarrier2, kCarrier2Firmware1Version);
  EXPECT_CALL(*modem, FlashMainFirmware(_, _)).Times(0);
  EXPECT_CALL(*modem, FlashCarrierFirmware(_, _)).Times(0);
  modem_flasher_->TryFlash(modem.get());

  // Suppose we swap the SIM back to the first one. Then we should try to
  // flash the generic firmware again.
  modem = GetDefaultModem();
  EXPECT_CALL(*modem, GetDeviceId()).Times(AtLeast(1));
  EXPECT_CALL(*modem, GetCarrierId()).Times(AtLeast(1));
  SetCarrierFirmwareInfo(modem.get(), kCarrier2, kCarrier2Firmware1Version);
  EXPECT_CALL(*modem, FlashMainFirmware(_, _)).Times(0);
  EXPECT_CALL(*modem, FlashCarrierFirmware(original_firmware,
                                           kGenericCarrierFirmware1Version))
      .WillOnce(Return(true));
  modem_flasher_->TryFlash(modem.get());
}

TEST_F(ModemFlasherTest, WritesToJournal) {
  base::FilePath new_firmware(kMainFirmware2Path);
  AddMainFirmwareFile(kDeviceId1, new_firmware, kMainFirmware2Version);

  auto modem = GetDefaultModem();
  EXPECT_CALL(*modem, GetDeviceId()).Times(AtLeast(1));
  EXPECT_CALL(*modem, GetMainFirmwareVersion()).Times(AtLeast(1));
  EXPECT_CALL(*modem, FlashMainFirmware(new_firmware, kMainFirmware2Version))
      .WillOnce(Return(true));
  EXPECT_CALL(*modem, FlashCarrierFirmware(_, _)).Times(0);
  EXPECT_CALL(*journal_, MarkStartOfFlashingMainFirmware(kDeviceId1, _))
      .Times(1);
  EXPECT_CALL(*journal_, MarkEndOfFlashingMainFirmware(kDeviceId1, _)).Times(1);
  EXPECT_CALL(*journal_, MarkStartOfFlashingCarrierFirmware(kDeviceId1, _))
      .Times(0);
  EXPECT_CALL(*journal_, MarkEndOfFlashingCarrierFirmware(kDeviceId1, _))
      .Times(0);
  base::Closure cb = modem_flasher_->TryFlash(modem.get());
  // The cleanup callback marks the end of flashing the firmware.
  ASSERT_FALSE(cb.is_null());
  cb.Run();
}

TEST_F(ModemFlasherTest, WritesToJournalOnFailure) {
  base::FilePath new_firmware(kMainFirmware2Path);
  AddMainFirmwareFile(kDeviceId1, new_firmware, kMainFirmware2Version);

  auto modem = GetDefaultModem();
  EXPECT_CALL(*modem, GetDeviceId()).Times(AtLeast(1));
  EXPECT_CALL(*modem, GetMainFirmwareVersion()).Times(AtLeast(1));
  EXPECT_CALL(*modem, FlashMainFirmware(new_firmware, kMainFirmware2Version))
      .WillOnce(Return(false));
  EXPECT_CALL(*modem, FlashCarrierFirmware(_, _)).Times(0);
  EXPECT_CALL(*journal_, MarkStartOfFlashingMainFirmware(kDeviceId1, _))
      .Times(1);
  EXPECT_CALL(*journal_, MarkEndOfFlashingMainFirmware(kDeviceId1, _)).Times(1);
  EXPECT_CALL(*journal_, MarkStartOfFlashingCarrierFirmware(kDeviceId1, _))
      .Times(0);
  EXPECT_CALL(*journal_, MarkEndOfFlashingCarrierFirmware(kDeviceId1, _))
      .Times(0);
  // There should be no journal cleanup after the flashing fails.
  base::Closure cb = modem_flasher_->TryFlash(modem.get());
  ASSERT_TRUE(cb.is_null());
}

TEST_F(ModemFlasherTest, WritesCarrierSwitchesToJournal) {
  base::FilePath original_firmware(kCarrier1Firmware1Path);
  base::FilePath other_firmware(kCarrier2Firmware1Path);
  AddCarrierFirmwareFile(kDeviceId1, kCarrier1, original_firmware,
                         kCarrier1Firmware1Version);
  AddCarrierFirmwareFile(kDeviceId1, kCarrier2, other_firmware,
                         kCarrier2Firmware1Version);

  auto modem = GetDefaultModem();
  EXPECT_CALL(*modem, GetDeviceId()).Times(AtLeast(1));
  EXPECT_CALL(*modem, GetCarrierId())
      .Times(AtLeast(1))
      .WillRepeatedly(Return(kCarrier2));
  SetCarrierFirmwareInfo(modem.get(), kCarrier1, kCarrier1Firmware1Version);
  EXPECT_CALL(*modem, FlashMainFirmware(_, _)).Times(0);
  EXPECT_CALL(*modem,
              FlashCarrierFirmware(other_firmware, kCarrier2Firmware1Version))
      .WillOnce(Return(true));
  EXPECT_CALL(*journal_, MarkStartOfFlashingMainFirmware(kDeviceId1, kCarrier2))
      .Times(0);
  EXPECT_CALL(*journal_, MarkEndOfFlashingMainFirmware(kDeviceId1, kCarrier2))
      .Times(0);
  EXPECT_CALL(*journal_,
              MarkStartOfFlashingCarrierFirmware(kDeviceId1, kCarrier2))
      .Times(1);
  EXPECT_CALL(*journal_,
              MarkEndOfFlashingCarrierFirmware(kDeviceId1, kCarrier2))
      .Times(1);
  base::Closure cb = modem_flasher_->TryFlash(modem.get());
  ASSERT_FALSE(cb.is_null());
  cb.Run();

  // After the modem reboots, the helper hopefully reports the new carrier.
  SetCarrierFirmwareInfo(modem.get(), kCarrier2, kCarrier2Firmware1Version);
  EXPECT_CALL(*modem, FlashMainFirmware(_, _)).Times(0);
  EXPECT_CALL(*modem, FlashCarrierFirmware(_, _)).Times(0);
  cb = modem_flasher_->TryFlash(modem.get());
  ASSERT_TRUE(cb.is_null());

  // Suppose we swap the SIM back to the first one. Then we should try to
  // flash the first firmware again.
  modem = GetDefaultModem();
  EXPECT_CALL(*modem, GetDeviceId()).Times(AtLeast(1));
  EXPECT_CALL(*modem, GetCarrierId()).Times(AtLeast(1));
  SetCarrierFirmwareInfo(modem.get(), kCarrier2, kCarrier2Firmware1Version);
  EXPECT_CALL(*modem, FlashMainFirmware(_, _)).Times(0);
  EXPECT_CALL(*modem, FlashCarrierFirmware(original_firmware,
                                           kCarrier1Firmware1Version))
      .WillOnce(Return(true));
  EXPECT_CALL(*journal_, MarkStartOfFlashingMainFirmware(kDeviceId1, _))
      .Times(0);
  EXPECT_CALL(*journal_, MarkEndOfFlashingMainFirmware(kDeviceId1, _)).Times(0);
  EXPECT_CALL(*journal_,
              MarkStartOfFlashingCarrierFirmware(kDeviceId1, kCarrier1))
      .Times(1);
  EXPECT_CALL(*journal_,
              MarkEndOfFlashingCarrierFirmware(kDeviceId1, kCarrier1))
      .Times(1);
  cb = modem_flasher_->TryFlash(modem.get());
  ASSERT_FALSE(cb.is_null());
  cb.Run();
}

TEST_F(ModemFlasherTest, CarrierSwitchingMainFirmware) {
  base::FilePath original_main(kMainFirmware1Path);
  AddMainFirmwareFile(kDeviceId1, original_main, kMainFirmware1Version);
  base::FilePath other_main(kMainFirmware2Path);
  AddMainFirmwareFileForCarrier(kDeviceId1, kCarrier2, other_main,
                                kMainFirmware2Version);

  base::FilePath original_carrier(kCarrier1Firmware1Path);
  base::FilePath other_carrier(kCarrier2Firmware1Path);
  AddCarrierFirmwareFile(kDeviceId1, kCarrier1, original_carrier,
                         kCarrier1Firmware1Version);
  AddCarrierFirmwareFile(kDeviceId1, kCarrier2, other_carrier,
                         kCarrier2Firmware1Version);

  auto modem = GetDefaultModem();
  EXPECT_CALL(*modem, GetDeviceId()).Times(AtLeast(1));
  EXPECT_CALL(*modem, GetCarrierId())
      .Times(AtLeast(1))
      .WillRepeatedly(Return(kCarrier2));
  SetCarrierFirmwareInfo(modem.get(), kCarrier1, kCarrier1Firmware1Version);
  EXPECT_CALL(*modem, FlashMainFirmware(other_main, kMainFirmware2Version))
      .WillOnce(Return(true));
  modem_flasher_->TryFlash(modem.get());

  // This will take two flash cycles -- one for main, one for carrier.
  modem = GetDefaultModem();
  EXPECT_CALL(*modem, GetDeviceId()).Times(AtLeast(1));
  EXPECT_CALL(*modem, GetCarrierId())
      .Times(AtLeast(1))
      .WillRepeatedly(Return(kCarrier2));
  EXPECT_CALL(*modem,
              FlashCarrierFirmware(other_carrier, kCarrier2Firmware1Version))
      .WillOnce(Return(true));
  modem_flasher_->TryFlash(modem.get());

  // Switch the carrier back and make sure we flash both firmware blobs
  // again.
  modem = GetDefaultModem();
  EXPECT_CALL(*modem, GetDeviceId()).Times(AtLeast(1));
  EXPECT_CALL(*modem, GetMainFirmwareVersion())
      .Times(AtLeast(1))
      .WillRepeatedly(Return(kMainFirmware2Version));
  EXPECT_CALL(*modem, GetCarrierId())
      .Times(AtLeast(1))
      .WillRepeatedly(Return(kCarrier1));
  SetCarrierFirmwareInfo(modem.get(), kCarrier2, kCarrier2Firmware1Version);
  EXPECT_CALL(*modem, FlashMainFirmware(original_main, kMainFirmware1Version))
      .WillOnce(Return(true));
  modem_flasher_->TryFlash(modem.get());

  // This will take two flash cycles -- one for main, one for carrier.
  modem = GetDefaultModem();
  EXPECT_CALL(*modem, GetDeviceId()).Times(AtLeast(1));
  EXPECT_CALL(*modem, GetCarrierId())
      .Times(AtLeast(1))
      .WillRepeatedly(Return(kCarrier1));
  EXPECT_CALL(*modem,
              FlashCarrierFirmware(original_carrier, kCarrier1Firmware1Version))
      .WillOnce(Return(true));
  modem_flasher_->TryFlash(modem.get());
}

TEST_F(ModemFlasherTest, InhibitDuringMainFirmwareFlash) {
  base::FilePath new_firmware(kMainFirmware2Path);
  AddMainFirmwareFile(kDeviceId1, new_firmware, kMainFirmware2Version);

  auto modem = GetDefaultModem();
  EXPECT_CALL(*modem, GetDeviceId()).Times(AtLeast(1));
  EXPECT_CALL(*modem, GetMainFirmwareVersion()).Times(AtLeast(1));
  EXPECT_CALL(*modem, FlashMainFirmware(new_firmware, kMainFirmware2Version))
      .WillOnce(Return(true));
  EXPECT_CALL(*modem, FlashCarrierFirmware(_, _)).Times(0);
  EXPECT_CALL(*modem, SetInhibited(true)).WillOnce(Return(true));
  EXPECT_CALL(*modem, SetInhibited(false)).WillOnce(Return(true));
  modem_flasher_->TryFlash(modem.get());
}

TEST_F(ModemFlasherTest, InhibitDuringCarrierFirmwareFlash) {
  base::FilePath new_firmware(kCarrier1Firmware2Path);
  AddCarrierFirmwareFile(kDeviceId1, kCarrier1, new_firmware,
                         kCarrier1Firmware2Version);

  auto modem = GetDefaultModem();
  EXPECT_CALL(*modem, GetDeviceId()).Times(AtLeast(1));
  SetCarrierFirmwareInfo(modem.get(), kCarrier1, kCarrier1Firmware1Version);
  EXPECT_CALL(*modem, FlashMainFirmware(_, _)).Times(0);
  EXPECT_CALL(*modem,
              FlashCarrierFirmware(new_firmware, kCarrier1Firmware2Version))
      .WillOnce(Return(true));
  EXPECT_CALL(*modem, SetInhibited(true)).WillOnce(Return(true));
  EXPECT_CALL(*modem, SetInhibited(false)).WillOnce(Return(true));
  modem_flasher_->TryFlash(modem.get());
}

TEST_F(ModemFlasherTest, SkipCarrierWithTwoUuidSameFirmware) {
  base::FilePath current_firmware(kCarrier1Firmware1Path);
  AddCarrierFirmwareFile(kDeviceId1, kCarrier1, current_firmware,
                         kCarrier1Firmware2Version);
  AddCarrierFirmwareFile(kDeviceId1, kCarrier1Mvno, current_firmware,
                         kCarrier1Firmware2Version);

  auto modem = GetDefaultModem();
  EXPECT_CALL(*modem, GetDeviceId()).Times(AtLeast(1));
  EXPECT_CALL(*modem, GetCarrierFirmwareVersion()).Times(AtLeast(1));
  // The modem will say that the currently flashed firmware has the carrier UUID
  // KCarrier1Mvno while the current carrier UUID is always returned as
  // kCarrier1.
  SetCarrierFirmwareInfo(modem.get(), kCarrier1Mvno, kCarrier1Firmware2Version);
  EXPECT_CALL(*modem, FlashMainFirmware(_, _)).Times(0);
  EXPECT_CALL(*modem, FlashCarrierFirmware(_, _)).Times(0);
  modem_flasher_->TryFlash(modem.get());
}

}  // namespace modemfwd
