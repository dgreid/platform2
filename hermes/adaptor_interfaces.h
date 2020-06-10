// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HERMES_ADAPTOR_INTERFACES_H_
#define HERMES_ADAPTOR_INTERFACES_H_

#include "hermes/dbus_bindings/org.chromium.Hermes.Euicc.h"

namespace hermes {

class EuiccAdaptorInterface : public org::chromium::Hermes::EuiccAdaptor {
 public:
  explicit EuiccAdaptorInterface(
      org::chromium::Hermes::EuiccInterface* interface)
      : org::chromium::Hermes::EuiccAdaptor(interface) {}
  virtual ~EuiccAdaptorInterface() = default;

  virtual dbus::ObjectPath object_path() const = 0;
};

}  // namespace hermes

#endif  // HERMES_ADAPTOR_INTERFACES_H_
