// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DLCSERVICE_TYPES_H_
#define DLCSERVICE_TYPES_H_

#include <map>
#include <string>
#include <unordered_set>
#include <utility>

#include <dlcservice/proto_bindings/dlcservice.pb.h>
#include <dbus/dlcservice/dbus-constants.h>

namespace dlcservice {

// |DlcId| is the ID of the DLC.
using DlcId = std::string;
// |DlcRoot| is the root within the mount point of the DLC.
using DlcRoot = std::string;
// |DlcInfo| holds information related to a DLC.
typedef struct DlcInfo {
  DlcInfo(DlcState::State state_in = DlcState::NOT_INSTALLED,
          std::string root_in = "",
          std::string err_code_in = kErrorNone);
  DlcState state;
  DlcRoot root;
} DlcInfo;
// |DlcMap| holds the mapping from |DlcId| to |DlcInfo|.
using DlcMap = std::map<DlcId, DlcInfo>;
// |DlcSet| holds |DlcID|s.
using DlcSet = std::unordered_set<DlcId>;

}  // namespace dlcservice

#endif  // DLCSERVICE_TYPES_H_
