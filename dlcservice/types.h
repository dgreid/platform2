// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DLCSERVICE_TYPES_H_
#define DLCSERVICE_TYPES_H_

#include <map>
#include <string>

namespace dlcservice {

// |DlcId| is the ID of the DLC.
using DlcId = std::string;
// |DlcRoot| is the root within the mount point of the DLC.
using DlcRoot = std::string;
// |DlcRootMap| holds the mapping from |DlcId| to |DlcRoot|.
using DlcRootMap = std::map<DlcId, DlcRoot>;

}  // namespace dlcservice

#endif  // DLCSERVICE_TYPES_H_
