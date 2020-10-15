// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "oobe_config/load_oobe_config_rollback.h"

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/json/json_writer.h>
#include <base/values.h>
#include <power_manager-client/power_manager/dbus-constants.h>

#include "oobe_config/oobe_config.h"
#include "oobe_config/rollback_constants.h"
#include "oobe_config/rollback_data.pb.h"

using base::FilePath;
using std::string;
using std::unique_ptr;

namespace oobe_config {

LoadOobeConfigRollback::LoadOobeConfigRollback(
    OobeConfig* oobe_config,
    bool allow_unencrypted,
    bool skip_reboot_for_testing,
    org::chromium::PowerManagerProxy* power_manager_proxy)
    : oobe_config_(oobe_config),
      allow_unencrypted_(allow_unencrypted),
      skip_reboot_for_testing_(skip_reboot_for_testing),
      power_manager_proxy_(power_manager_proxy) {}

bool LoadOobeConfigRollback::GetOobeConfigJson(string* config,
                                               string* enrollment_domain) {
  LOG(INFO) << "Looking for rollback state.";

  *config = "";
  *enrollment_domain = "";

  // Precondition for running rollback.
  if (!oobe_config_->FileExists(kRestoreTempPath)) {
    LOG(ERROR) << "Restore destination path doesn't exist.";
    return false;
  }

  if (!(oobe_config_->CheckFirstStage() || oobe_config_->CheckThirdStage())) {
    // We are not in a valid state to run either stage 1 or stage 3 of rollback
    // this is either a real error or this is not a rollback.

    if (oobe_config_->CheckSecondStage()) {
      // This shouldn't happen, the script failed to execute. We fail and return
      // false.
      LOG(ERROR) << "Rollback restore is in invalid state (stage 2).";
      metrics_.RecordRestoreResult(Metrics::OobeRestoreResult::kStage2Failure);
    }
    return false;
  }

  if (oobe_config_->CheckFirstStage()) {
    LOG(INFO) << "Starting rollback restore stage 1.";

    // In the first stage we decrypt the proto from kUnencryptedRollbackDataPath
    // and save it unencrypted to kEncryptedStatefulRollbackDataPath.
    bool restore_result;
    if (allow_unencrypted_) {
      restore_result = oobe_config_->UnencryptedRollbackRestore();
    } else {
      restore_result = oobe_config_->EncryptedRollbackRestore();
    }

    if (!restore_result) {
      LOG(ERROR) << "Failed to restore rollback data";
      metrics_.RecordRestoreResult(Metrics::OobeRestoreResult::kStage1Failure);
      return false;
    }

    // We create kFirstStageCompletedFile after this.
    oobe_config_->WriteFile(kFirstStageCompletedFile, "");
    // If all succeeded, we reboot.
    if (base::PathExists(kFirstStageCompletedFile) && power_manager_proxy_) {
      if (!skip_reboot_for_testing_) {
        LOG(INFO) << "Rebooting device.";
        brillo::ErrorPtr error;
        if (!power_manager_proxy_->RequestRestart(
                ::power_manager::REQUEST_RESTART_OTHER,
                "oobe_config: reboot after rollback restore first stage",
                &error)) {
          LOG(ERROR) << "Failed to reboot device, error: "
                     << error->GetMessage();
          metrics_.RecordRestoreResult(
              Metrics::OobeRestoreResult::kStage1Failure);
        }
      } else {
        LOG(INFO) << "Skipping reboot for testing";
      }
    }
    exit(0);
  }

  if (oobe_config_->CheckThirdStage()) {
    LOG(INFO) << "Starting rollback restore stage 3.";

    // We load the proto from kEncryptedStatefulRollbackDataPath.
    string rollback_data_str;
    if (!oobe_config_->ReadFile(kEncryptedStatefulRollbackDataPath,
                                &rollback_data_str)) {
      metrics_.RecordRestoreResult(Metrics::OobeRestoreResult::kStage3Failure);
      return false;
    }
    RollbackData rollback_data;
    if (!rollback_data.ParseFromString(rollback_data_str)) {
      LOG(ERROR) << "Couldn't parse proto.";
      metrics_.RecordRestoreResult(Metrics::OobeRestoreResult::kStage3Failure);
      return false;
    }
    // We get the data for Chrome and assemble the config.
    if (!AssembleConfig(rollback_data, config)) {
      LOG(ERROR) << "Failed to assemble config.";
      metrics_.RecordRestoreResult(Metrics::OobeRestoreResult::kStage3Failure);
      return false;
    }

    // If it succeeded, we remove all files from
    // kEncryptedStatefulRollbackDataPath.
    LOG(INFO) << "Cleaning up rollback data.";
    oobe_config_->CleanupEncryptedStatefulDirectory();

    LOG(INFO) << "Rollback restore completed successfully.";
    metrics_.RecordRestoreResult(Metrics::OobeRestoreResult::kSuccess);
    return true;
  }

  NOTREACHED();
  return false;
}

bool LoadOobeConfigRollback::AssembleConfig(const RollbackData& rollback_data,
                                            string* config) {
  // Possible values are defined in
  // chrome/browser/resources/chromeos/login/oobe_types.js.
  // TODO(zentaro): Export these strings as constants.
  base::Value dictionary(base::Value::Type::DICTIONARY);
  // Always skip next screen.
  dictionary.SetBoolKey("welcomeNext", true);
  // Always skip network selection screen if possible.
  dictionary.SetBoolKey("networkUseConnected", true);
  // We don't want updates after rolling back.
  dictionary.SetBoolKey("updateSkipNonCritical", true);
  // Set whether metrics should be enabled if it exists in |rollback_data|.
  dictionary.SetBoolKey("eulaSendStatistics",
                        rollback_data.eula_send_statistics());
  // Set whether the EULA as already accepted and can be skipped if the field is
  // present in |rollback_data|.
  dictionary.SetBoolKey("eulaAutoAccept", rollback_data.eula_auto_accept());
  // Tell Chrome that it still has to create some robot accounts that were
  // destroyed during rollback.
  dictionary.SetBoolKey("enrollmentRestoreAfterRollback", true);

  return base::JSONWriter::Write(dictionary, config);
}

}  // namespace oobe_config
