// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Library to provide access to the Chrome OS master configuration

#ifndef CHROMEOS_CONFIG_LIBCROS_CONFIG_CROS_CONFIG_IMPL_H_
#define CHROMEOS_CONFIG_LIBCROS_CONFIG_CROS_CONFIG_IMPL_H_

#include <string>

#include <base/macros.h>
#include "chromeos-config/libcros_config/cros_config_interface.h"
#include "chromeos-config/libcros_config/identity.h"

namespace base {
class FilePath;
}  // namespace base

namespace brillo {

class CrosConfigImpl : public CrosConfigInterface {
 public:
  CrosConfigImpl();
  CrosConfigImpl(const CrosConfigImpl&) = delete;
  CrosConfigImpl& operator=(const CrosConfigImpl&) = delete;

  ~CrosConfigImpl() override;

  // Read the config into our internal structures
  // @filepath: path to configuration file (e.g. .dtb).
  // @return true if OK, false on error (which is logged)
  virtual bool ReadConfigFile(const base::FilePath& filepath) = 0;

  // Select the config to use based on the device identity.
  // @identity: identity from the device to match
  // @return true on success, false on failure
  virtual bool SelectConfigByIdentity(const CrosConfigIdentity& identity) = 0;

 protected:
  // Runs a quick init check and prints an error to stderr if it fails.
  // @return true if OK, false on error.
  bool InitCheck() const;

  bool inited_ = false;  // true if the class is ready for use (Init*ed)
};

}  // namespace brillo

#endif  // CHROMEOS_CONFIG_LIBCROS_CONFIG_CROS_CONFIG_IMPL_H_
