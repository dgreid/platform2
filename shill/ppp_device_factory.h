// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_PPP_DEVICE_FACTORY_H_
#define SHILL_PPP_DEVICE_FACTORY_H_

#include <string>

#include <base/macros.h>
#include <base/no_destructor.h>

namespace shill {

class Manager;
class PPPDevice;

class PPPDeviceFactory {
 public:
  virtual ~PPPDeviceFactory();

  // This is a singleton. Use PPPDeviceFactory::GetInstance()->Foo().
  static PPPDeviceFactory* GetInstance();

  virtual PPPDevice* CreatePPPDevice(Manager* manager,
                                     const std::string& link_name,
                                     int interface_index);

 protected:
  PPPDeviceFactory();
  PPPDeviceFactory(const PPPDeviceFactory&) = delete;
  PPPDeviceFactory& operator=(const PPPDeviceFactory&) = delete;

 private:
  friend class base::NoDestructor<PPPDeviceFactory>;
};

}  // namespace shill

#endif  // SHILL_PPP_DEVICE_FACTORY_H_
