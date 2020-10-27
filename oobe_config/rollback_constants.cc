// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "oobe_config/rollback_constants.h"

namespace oobe_config {

const base::FilePath kStatefulPartition =
    base::FilePath("/mnt/stateful_partition/");

const base::FilePath kSaveTempPath =
    base::FilePath("/var/lib/oobe_config_save/");
const base::FilePath kRestoreTempPath =
    base::FilePath("/var/lib/oobe_config_restore/");
const base::FilePath kUnencryptedStatefulRollbackDataPath = base::FilePath(
    "/mnt/stateful_partition/unencrypted/preserve/rollback_data");
const base::FilePath kEncryptedStatefulRollbackDataPath =
    base::FilePath("/var/lib/oobe_config_restore/rollback_data");

const base::FilePath kFirstStageCompletedFile =
    base::FilePath("/var/lib/oobe_config_restore/first_stage_completed");
const base::FilePath kSecondStageCompletedFile =
    base::FilePath("/var/lib/oobe_config_restore/second_stage_completed");

const base::FilePath kRollbackSaveMarkerFile =
    base::FilePath("/mnt/stateful_partition/.save_rollback_data");

const base::FilePath kOobeCompletedFile =
    base::FilePath("/home/chronos/.oobe_completed");
const char kOobeCompletedFileName[] = ".oobe_completed";

const base::FilePath kMetricsReportingEnabledFile =
    base::FilePath("/home/chronos/Consent To Send Stats");
const char kMetricsReportingEnabledFileName[] = "Consent To Send Stats";

const base::FilePath kInstallAttributesPath =
    base::FilePath("/home/.shadow/install_attributes.pb");
const char kInstallAttributesFileName[] = "install_attributes.pb";

const base::FilePath kOwnerKeyFilePath =
    base::FilePath("/var/lib/whitelist/owner.key");
const char kOwnerKeyFileName[] = "owner.key";

const base::FilePath kPolicyFileDirectory =
    base::FilePath("/var/lib/whitelist/");
const char kPolicyFileName[] = "policy";
const char kPolicyFileNamePattern[] = "policy*";

const base::FilePath kShillDefaultProfilePath =
    base::FilePath("/var/cache/shill/default.profile");
const char kShillDefaultProfileFileName[] = "default.profile";

const char kPolicyDotOneFileNameForTesting[] = "policy.1";

const char kOobeConfigSaveUsername[] = "oobe_config_save";
const char kRootUsername[] = "root";
const char kPreserveGroupName[] = "preserve";

const base::FilePath kDataSavedFile =
    base::FilePath("/var/lib/oobe_config_save/.data_saved");

}  // namespace oobe_config
