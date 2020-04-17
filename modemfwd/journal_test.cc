// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modemfwd/journal.h"

#include <memory>
#include <string>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <gtest/gtest.h>

#include "modemfwd/firmware_directory_stub.h"
#include "modemfwd/mock_modem_helper.h"
#include "modemfwd/modem_helper_directory_stub.h"
#include "modemfwd/scoped_temp_file.h"

using ::testing::_;
using ::testing::Return;

namespace modemfwd {

namespace {

constexpr char kDeviceId[] = "foobar";
constexpr char kCarrierId[] = "carrier";

constexpr char kMainFirmwarePath[] = "main_firmware.fls";
constexpr char kMainFirmwareVersion[] = "1.0";

constexpr char kCarrierFirmwarePath[] = "carrier_firmware.fls";
constexpr char kCarrierFirmwareVersion[] = "1.0";

}  // namespace

class JournalTest : public ::testing::Test {
 public:
  JournalTest()
      : journal_file_(ScopedTempFile::Create()),
        firmware_directory_(new FirmwareDirectoryStub),
        modem_helper_directory_(new ModemHelperDirectoryStub) {
    CHECK(journal_file_);
    EXPECT_CALL(modem_helper_, GetFirmwareInfo(_)).Times(0);
    modem_helper_directory_->AddHelper(kDeviceId, &modem_helper_);
  }

 protected:
  void SetUpJournal(const std::string& journal_text) {
    CHECK_EQ(base::WriteFile(journal_file_->path(), journal_text.data(),
                             journal_text.size()),
             journal_text.size());
  }

  std::unique_ptr<Journal> GetJournal() {
    return OpenJournal(journal_file_->path(), firmware_directory_.get(),
                       modem_helper_directory_.get());
  }

  void AddMainFirmwareFile(const base::FilePath& firmware_path,
                           const std::string& version) {
    FirmwareFileInfo firmware_info(firmware_path, version);
    firmware_directory_->AddMainFirmware(kDeviceId, firmware_info);
  }

  void AddCarrierFirmwareFile(const base::FilePath& firmware_path,
                              const std::string& version) {
    FirmwareFileInfo firmware_info(firmware_path, version);
    firmware_directory_->AddCarrierFirmware(kDeviceId, kCarrierId,
                                            firmware_info);
  }

  MockModemHelper modem_helper_;

 private:
  std::unique_ptr<ScopedTempFile> journal_file_;
  std::unique_ptr<FirmwareDirectoryStub> firmware_directory_;
  std::unique_ptr<ModemHelperDirectoryStub> modem_helper_directory_;
};

TEST_F(JournalTest, EmptyJournal) {
  EXPECT_CALL(modem_helper_, FlashMainFirmware(_, _)).Times(0);
  EXPECT_CALL(modem_helper_, FlashCarrierFirmware(_, _)).Times(0);
  GetJournal();
}

TEST_F(JournalTest, PriorRunWasNotInterrupted_Main) {
  auto journal = GetJournal();
  journal->MarkStartOfFlashingMainFirmware(kDeviceId, kCarrierId);
  journal->MarkEndOfFlashingMainFirmware(kDeviceId, kCarrierId);

  EXPECT_CALL(modem_helper_, FlashMainFirmware(_, _)).Times(0);
  EXPECT_CALL(modem_helper_, FlashCarrierFirmware(_, _)).Times(0);
  // Getting a new journal simulates a crash or shutdown.
  journal = GetJournal();
}

TEST_F(JournalTest, PriorRunWasInterrupted_Main) {
  const base::FilePath main_fw_path(kMainFirmwarePath);
  AddMainFirmwareFile(main_fw_path, kMainFirmwareVersion);

  auto journal = GetJournal();
  journal->MarkStartOfFlashingMainFirmware(kDeviceId, kCarrierId);

  EXPECT_CALL(modem_helper_,
              FlashMainFirmware(main_fw_path, kMainFirmwareVersion))
      .WillOnce(Return(true));
  EXPECT_CALL(modem_helper_, FlashCarrierFirmware(_, _)).Times(0);
  journal = GetJournal();

  // Test that the journal is cleared afterwards, so we don't try to
  // flash a second time if we crash again.
  EXPECT_CALL(modem_helper_, FlashMainFirmware(_, _)).Times(0);
  EXPECT_CALL(modem_helper_, FlashCarrierFirmware(_, _)).Times(0);
  journal = GetJournal();
}

TEST_F(JournalTest, PriorRunWasNotInterrupted_Carrier) {
  auto journal = GetJournal();
  journal->MarkStartOfFlashingCarrierFirmware(kDeviceId, kCarrierId);
  journal->MarkEndOfFlashingCarrierFirmware(kDeviceId, kCarrierId);

  EXPECT_CALL(modem_helper_, FlashMainFirmware(_, _)).Times(0);
  EXPECT_CALL(modem_helper_, FlashCarrierFirmware(_, _)).Times(0);
  // Getting a new journal simulates a crash or shutdown.
  journal = GetJournal();
}

TEST_F(JournalTest, PriorRunWasInterrupted_Carrier) {
  const base::FilePath carrier_fw_path(kCarrierFirmwarePath);
  AddCarrierFirmwareFile(carrier_fw_path, kCarrierFirmwareVersion);

  auto journal = GetJournal();
  journal->MarkStartOfFlashingCarrierFirmware(kDeviceId, kCarrierId);

  EXPECT_CALL(modem_helper_, FlashMainFirmware(_, _)).Times(0);
  EXPECT_CALL(modem_helper_,
              FlashCarrierFirmware(carrier_fw_path, kCarrierFirmwareVersion))
      .WillOnce(Return(true));
  journal = GetJournal();

  // Test that the journal is cleared afterwards, so we don't try to
  // flash a second time if we crash again.
  EXPECT_CALL(modem_helper_, FlashMainFirmware(_, _)).Times(0);
  EXPECT_CALL(modem_helper_, FlashCarrierFirmware(_, _)).Times(0);
  journal = GetJournal();
}

TEST_F(JournalTest, IgnoreMalformedJournalEntries) {
  SetUpJournal("blahblah");
  EXPECT_CALL(modem_helper_, FlashMainFirmware(_, _)).Times(0);
  EXPECT_CALL(modem_helper_, FlashCarrierFirmware(_, _)).Times(0);
  GetJournal();
}

TEST_F(JournalTest, MultipleEntries) {
  auto journal = GetJournal();
  journal->MarkStartOfFlashingMainFirmware(kDeviceId, kCarrierId);
  journal->MarkEndOfFlashingMainFirmware(kDeviceId, kCarrierId);
  journal->MarkStartOfFlashingCarrierFirmware(kDeviceId, kCarrierId);
  journal->MarkEndOfFlashingCarrierFirmware(kDeviceId, kCarrierId);

  EXPECT_CALL(modem_helper_, FlashMainFirmware(_, _)).Times(0);
  EXPECT_CALL(modem_helper_, FlashCarrierFirmware(_, _)).Times(0);
  journal = GetJournal();
}

}  // namespace modemfwd
