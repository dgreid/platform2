// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modemfwd/modem_flasher.h"

#include <memory>
#include <utility>

#include <base/files/file_util.h>
#include <base/stl_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>

#include "modemfwd/firmware_file.h"
#include "modemfwd/logging.h"
#include "modemfwd/modem.h"

namespace {

constexpr char kDisableAutoUpdatePref[] =
    "/var/lib/modemfwd/disable_auto_update";

}  // namespace

namespace modemfwd {

namespace {

class InhibitMode {
 public:
  explicit InhibitMode(Modem* modem) : modem_(modem) {
    if (!modem_->SetInhibited(true))
      ELOG(INFO) << "Inhibiting failed";
  }
  ~InhibitMode() {
    if (!modem_->SetInhibited(false))
      ELOG(INFO) << "Uninhibiting failed";
  }

 private:
  Modem* modem_;
};

}  // namespace

bool IsAutoUpdateDisabledByPref() {
  const base::FilePath pref_path(kDisableAutoUpdatePref);
  std::string contents;
  if (!base::ReadFileToString(pref_path, &contents))
    return false;

  contents = base::TrimWhitespaceASCII(contents, base::TRIM_ALL).as_string();
  int pref_value;
  if (!base::StringToInt(contents, &pref_value))
    return false;

  return (pref_value == 1);
}

ModemFlasher::ModemFlasher(
    std::unique_ptr<FirmwareDirectory> firmware_directory,
    std::unique_ptr<Journal> journal)
    : firmware_directory_(std::move(firmware_directory)),
      journal_(std::move(journal)) {}

base::Closure ModemFlasher::TryFlash(Modem* modem) {
  if (IsAutoUpdateDisabledByPref()) {
    LOG(INFO) << "Update disabled by pref";
    return base::Closure();
  }

  std::string equipment_id = modem->GetEquipmentId();
  FlashState* flash_state = &modem_info_[equipment_id];
  if (!flash_state->ShouldFlash()) {
    LOG(WARNING) << "Modem with equipment ID \"" << equipment_id
                 << "\" failed to flash too many times; not flashing";
    return base::Closure();
  }

  std::string device_id = modem->GetDeviceId();
  std::string current_carrier = modem->GetCarrierId();
  // The real carrier ID before it might be replaced by the generic one
  std::string real_carrier = current_carrier;
  flash_state->OnCarrierSeen(current_carrier);
  FirmwareDirectory::Files files = firmware_directory_->FindFirmware(
      device_id, current_carrier.empty() ? nullptr : &current_carrier);

  // Check if we need to update the main firmware.
  if (flash_state->ShouldFlashMainFirmware() &&
      files.main_firmware.has_value()) {
    const FirmwareFileInfo& file_info = files.main_firmware.value();
    ELOG(INFO) << "Found main firmware blob " << file_info.version
               << ", currently installed main firmware version: "
               << modem->GetMainFirmwareVersion();
    if (file_info.version == modem->GetMainFirmwareVersion()) {
      // We don't need to check the main firmware again if there's nothing new.
      // Pretend that we successfully flashed it.
      flash_state->OnFlashedMainFirmware();
    } else {
      FirmwareFile firmware_file;
      if (!firmware_file.PrepareFrom(file_info))
        return base::Closure();

      // We found different firmware! Flash the modem, and since it will
      // reboot afterwards, we can wait to get called again to check the
      // carrier firmware.
      InhibitMode _inhibit(modem);
      journal_->MarkStartOfFlashingMainFirmware(device_id, current_carrier);
      if (modem->FlashMainFirmware(firmware_file.path_on_filesystem(),
                                   file_info.version)) {
        // Refer to |firmware_file.path_for_logging()| in the log and journal.
        flash_state->OnFlashedMainFirmware();
        ELOG(INFO) << "Flashed " << firmware_file.path_for_logging().value()
                   << " to the modem";
        return base::Bind(&Journal::MarkEndOfFlashingMainFirmware,
                          base::Unretained(journal_.get()), device_id,
                          current_carrier);
      } else {
        flash_state->OnFlashFailed();
        journal_->MarkEndOfFlashingMainFirmware(device_id, current_carrier);
        return base::Closure();
      }
    }
  }

  // If there's no SIM, we can stop here.
  if (current_carrier.empty()) {
    ELOG(INFO) << "No carrier found. Is a SIM card inserted?";
    return base::Closure();
  }

  // Clear the attach APN if needed for a specific modem/carrier combination.
  if (!modem->ClearAttachAPN(real_carrier)) {
    ELOG(INFO) << "Clear attach APN failed for current carrier.";
  }

  // Check if we have carrier firmware matching the SIM's carrier. If not,
  // there's nothing to flash.
  if (!files.carrier_firmware.has_value()) {
    ELOG(INFO) << "No carrier firmware found for carrier " << current_carrier;
    return base::Closure();
  }

  const FirmwareFileInfo& file_info = files.carrier_firmware.value();
  if (!flash_state->ShouldFlashCarrierFirmware(file_info.firmware_path)) {
    ELOG(INFO) << "Already flashed carrier firmware for " << current_carrier;
    return base::Closure();
  }

  ELOG(INFO) << "Found carrier firmware blob " << file_info.version
             << " for carrier " << current_carrier;

  // Carrier firmware operates a bit differently. We need to flash if
  // the carrier or the version has changed, or if there wasn't any carrier
  // firmware to begin with.
  std::string carrier_fw_id = modem->GetCarrierFirmwareId();
  std::string carrier_fw_version = modem->GetCarrierFirmwareVersion();
  bool has_carrier_fw = !(carrier_fw_id.empty() || carrier_fw_version.empty());
  if (has_carrier_fw) {
    ELOG(INFO) << "Currently installed carrier firmware version "
               << carrier_fw_version << " for carrier " << carrier_fw_id;
  } else {
    ELOG(INFO) << "No carrier firmware is currently installed";
  }

  if (!has_carrier_fw || carrier_fw_id != current_carrier ||
      carrier_fw_version != file_info.version) {
    FirmwareFile firmware_file;
    if (!firmware_file.PrepareFrom(file_info))
      return base::Closure();

    InhibitMode _inhibit(modem);
    journal_->MarkStartOfFlashingCarrierFirmware(device_id, current_carrier);
    if (modem->FlashCarrierFirmware(firmware_file.path_on_filesystem(),
                                    file_info.version)) {
      // Refer to |firmware_file.path_for_logging()| in the log and journal.
      flash_state->OnFlashedCarrierFirmware(firmware_file.path_for_logging());
      ELOG(INFO) << "Flashed " << firmware_file.path_for_logging().value()
                 << " to the modem";
      return base::Bind(&Journal::MarkEndOfFlashingCarrierFirmware,
                        base::Unretained(journal_.get()), device_id,
                        current_carrier);
    } else {
      flash_state->OnFlashFailed();
      journal_->MarkEndOfFlashingCarrierFirmware(device_id, current_carrier);
      return base::Closure();
    }
  }

  return base::Closure();
}

}  // namespace modemfwd
