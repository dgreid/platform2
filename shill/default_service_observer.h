// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_DEFAULT_SERVICE_OBSERVER_H_
#define SHILL_DEFAULT_SERVICE_OBSERVER_H_

#include "shill/refptr_types.h"

#include <base/observer_list_types.h>

namespace shill {

// Interface for Observer of default Service changes. Registered and
// unregistered using Manager::{Add,Remove}DefaultServiceObserver.
class DefaultServiceObserver : public base::CheckedObserver {
 public:
  virtual ~DefaultServiceObserver() = default;

  // This event is triggered when the logical and/or physical default Service
  // has changed.
  //
  // Note: It is feasible in the future that we would actually have a chain of
  // default Services rather than just two (e.g. VPNService -> (virtual)
  // PPPoEService -> EthernetService). For now, the implicit assumption in a
  // number of parts of Shill is that this chain of default Services can be at
  // most two distinct Services.
  //
  // TODO(crbug.com/999589) Once lower Device is fully implemented, VPNDrivers
  // can use their VirtualDevice instance to get the specific events they are
  // looking for and the two bools can be removed.
  virtual void OnDefaultServiceChanged(const ServiceRefPtr& logical_service,
                                       bool logical_service_changed,
                                       const ServiceRefPtr& physical_service,
                                       bool physical_service_changed) = 0;
};

}  // namespace shill

#endif  // SHILL_DEFAULT_SERVICE_OBSERVER_H_
