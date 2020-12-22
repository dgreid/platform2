// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TYPECD_PARTNER_H_
#define TYPECD_PARTNER_H_

#include <map>
#include <memory>
#include <utility>

#include <base/files/file_path.h>
#include <gtest/gtest_prod.h>

#include "typecd/alt_mode.h"
#include "typecd/peripheral.h"

namespace typecd {

// A partner represents a device which is connected to the host. This
// class is used to maintain the state associated with the partner.
class Partner : public Peripheral {
 public:
  explicit Partner(const base::FilePath& syspath);
  Partner(const Partner&) = delete;
  Partner& operator=(const Partner&) = delete;

  // Check if a particular alt mode index (as specified by the Type C connector
  // class framework) is registered.
  bool IsAltModePresent(int index);

  bool AddAltMode(const base::FilePath& mode_syspath);
  void RemoveAltMode(const base::FilePath& mode_syspath);

  // In some cases, some of the PD identity info (like number of alternate
  // modes) is not yet available when the Partner is first created. When these
  // later get added, a udev event occurs. When this event occurs, read sysfs to
  // get this data if it is available.
  void UpdatePDInfoFromSysfs();

  // Return the total number of AltModes supported by the partner. If this value
  // hasn't been populated yet, the default value is -1, signifying that
  // discovery is not yet complete.
  int GetNumAltModes() { return num_alt_modes_; }

  // Set the total number of alternate modes supported by the partner.
  void SetNumAltModes(int num_alt_modes) { num_alt_modes_ = num_alt_modes; }

  // Parse the number of alternate modes supported by the partner. This value
  // should be populated from the corresponding file in sysfs.
  //
  // Returns the number of supported alternate modes, or -1 if the sysfs file is
  // unavailable.
  int ParseNumAltModes();

  // Return the AltMode with index |index|, and nullptr if such an AltMode
  // doesn't exist.
  AltMode* GetAltMode(int index);

  // Checks whether partner PD discovery is complete (and we have all the PD
  // information that the kernel can provide). To determine this, we check
  // whether the number of registered altmodes equals the |num_alt_modes_| value
  // which is read from sysfs.
  bool DiscoveryComplete();

 private:
  // A map representing all the alternate modes supported by the partner.
  // The key is the index of the alternate mode as determined by the connector
  // class sysfs directories that represent them. For example, and alternate
  // mode which has the directory
  // "/sys/class/typec/port1-partner/port1-partner.0" will use an key of "0".
  std::map<int, std::unique_ptr<AltMode>> alt_modes_;
  int num_alt_modes_;
};

}  // namespace typecd

#endif  // TYPECD_PARTNER_H_
