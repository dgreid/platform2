// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERMISSION_BROKER_UDEV_SCOPERS_H_
#define PERMISSION_BROKER_UDEV_SCOPERS_H_

#include <libudev.h>

#include "base/memory/scoped_ptr.h"

namespace permission_broker {

struct UdevDeleter {
  void operator()(udev* udev) const;
};

struct UdevEnumerateDeleter {
  void operator()(udev_enumerate* enumerate) const;
};

struct UdevDeviceDeleter {
  void operator()(udev_device* device) const;
};

typedef scoped_ptr<udev, UdevDeleter> ScopedUdevPtr;
typedef scoped_ptr<udev_enumerate, UdevEnumerateDeleter> ScopedUdevEnumeratePtr;
typedef scoped_ptr<udev_device, UdevDeviceDeleter> ScopedUdevDevicePtr;

}  // namespace permission_broker

#endif  // PERMISSION_BROKER_UDEV_SCOPERS_H_
