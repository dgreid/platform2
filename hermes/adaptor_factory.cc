// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <memory>

#include "hermes/adaptor_factory.h"
#include "hermes/manager_dbus_adaptor.h"

namespace hermes {

std::unique_ptr<org::chromium::Hermes::ManagerAdaptor>
AdaptorFactory::CreateManagerAdaptor(Manager* manager) {
  return std::make_unique<ManagerDBusAdaptor>(manager);
}

}  // namespace hermes
