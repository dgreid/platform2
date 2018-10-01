// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "oobe_config/oobe_config_restore_service.h"

#include <string>

#include "oobe_config/load_oobe_config_rollback.h"
#include "oobe_config/oobe_config.h"
#include "oobe_config/proto_bindings/oobe_config.pb.h"

using brillo::dbus_utils::AsyncEventSequencer;

namespace oobe_config {

namespace {

// Serializes |proto| to the byte array |proto_blob|.
template <typename Proto>
bool SerializeProtoToBlob(const Proto& proto, ProtoBlob* proto_blob) {
  DCHECK(proto_blob);
  proto_blob->resize(proto.ByteSizeLong());
  return proto.SerializeToArray(proto_blob->data(), proto.ByteSizeLong());
}

}  // namespace

OobeConfigRestoreService::OobeConfigRestoreService(
    std::unique_ptr<brillo::dbus_utils::DBusObject> dbus_object,
    bool allow_unencrypted)
    : org::chromium::OobeConfigRestoreAdaptor(this),
      dbus_object_(std::move(dbus_object)),
      allow_unencrypted_(allow_unencrypted) {}

OobeConfigRestoreService::~OobeConfigRestoreService() = default;

void OobeConfigRestoreService::RegisterAsync(
    const AsyncEventSequencer::CompletionAction& completion_callback) {
  RegisterWithDBusObject(dbus_object_.get());
  dbus_object_->RegisterAsync(completion_callback);
}

void OobeConfigRestoreService::ProcessAndGetOobeAutoConfig(
    int32_t* error, ProtoBlob* oobe_config_blob) {
  DCHECK(error);
  DCHECK(oobe_config_blob);

  OobeConfig oobe_config;
  LoadOobeConfigRollback load_oobe_config_rollback(
      &oobe_config, allow_unencrypted_, /*execute_commands=*/true);
  std::string chrome_config_json, unused_enrollment_domain;
  if (load_oobe_config_rollback.GetOobeConfigJson(&chrome_config_json,
                                                  &unused_enrollment_domain)) {
    LOG(WARNING) << "Rollback oobe config sent: " << chrome_config_json;
  } else {
    LOG(WARNING) << "Rollback oobe config not found.";
  }
  // TODO(ahassani): Add USB restore too.
  OobeRestoreData data_proto;
  data_proto.set_chrome_config_json(chrome_config_json);
  *error = SerializeProtoToBlob(data_proto, oobe_config_blob) ? 0 : -1;
}

}  // namespace oobe_config
