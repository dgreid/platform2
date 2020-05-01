// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_SYSTEM_AMBIENT_LIGHT_OBSERVER_H_
#define POWER_MANAGER_POWERD_SYSTEM_AMBIENT_LIGHT_OBSERVER_H_

#include <base/observer_list_types.h>

namespace power_manager {
namespace system {

class AmbientLightSensorInterface;

// Interface for classes interested in receiving updates about the ambient
// light level from AmbientLightSensor.
class AmbientLightObserver : public base::CheckedObserver {
 public:
  virtual ~AmbientLightObserver() {}

  // Called when the light level is measured. The measured level may be
  // unchanged from the previously-observed level.
  virtual void OnAmbientLightUpdated(AmbientLightSensorInterface* sensor) = 0;
};

}  // namespace system
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_SYSTEM_AMBIENT_LIGHT_OBSERVER_H_
