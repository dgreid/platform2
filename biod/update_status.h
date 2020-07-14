// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BIOD_UPDATE_STATUS_H_
#define BIOD_UPDATE_STATUS_H_

namespace biod {
namespace updater {

enum class UpdateStatus {
  kUpdateNotNecessary,
  kUpdateSucceeded,
  kUpdateFailedGetVersion,
  kUpdateFailedFlashProtect,
  kUpdateFailedRO,
  kUpdateFailedRW,
};

}  // namespace updater
}  // namespace biod

#endif  // BIOD_UPDATE_STATUS_H_
