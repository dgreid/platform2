// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_MINIJAIL_MINIJAIL_CONFIGURATION_H_
#define DIAGNOSTICS_CROS_HEALTHD_MINIJAIL_MINIJAIL_CONFIGURATION_H_

namespace diagnostics {

// Configures cros_healthd's minijail, then enters it. Any errors encountered
// during configuration result in a CHECK, and the daemon will crash rather than
// start without a sandbox.
void ConfigureAndEnterMinijail();

// Enters a new mount namespace. We don't want anyone other than our descendants
// to see our tmpfs.
void NewMountNamespace();

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_MINIJAIL_MINIJAIL_CONFIGURATION_H_
