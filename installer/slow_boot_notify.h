// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INSTALLER_SLOW_BOOT_NOTIFY_H_
#define INSTALLER_SLOW_BOOT_NOTIFY_H_

// In case of firmware update, return true if slow boot notification has to be
// generated, else return false.
bool SlowBootNotifyRequired();

#endif  // INSTALLER_SLOW_BOOT_NOTIFY_H_
