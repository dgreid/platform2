// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TYPECD_CABLE_H_
#define TYPECD_CABLE_H_

#include <map>
#include <memory>
#include <utility>

#include <base/files/file_path.h>
#include <gtest/gtest_prod.h>

#include "typecd/peripheral.h"

namespace typecd {

// A cable represents a cord/connector which is used to connect a Partner
// to a Host. This class is used to maintain the state associated with the
// cable.
class Cable : public Peripheral {
 public:
  explicit Cable(const base::FilePath& syspath) : Peripheral(syspath) {}
  Cable(const Cable&) = delete;
  Cable& operator=(const Cable&) = delete;

  // Add an alternate mode for the plug associated with the cable.
  // NOTE: We currently only process SOP' plugs.
  // TODO(b/159859845): Add support for SOP'' plugs and alternate modes.
  bool AddAltMode(const base::FilePath& mode_syspath);
  void RemoveAltMode(const base::FilePath& mode_syspath);

  // Return the total number of SOP' alternate modes supported by the cable. If
  // this value hasn't been populated yet, the default value is -1, signifying
  // that discovery is not yet complete.
  int GetNumAltModes() { return num_alt_modes_; }

  // Set the total number of SOP' alternate modes supported by the cable. This
  // value should be populated either:
  // - From the corresponding file in sysfs
  //   <or>
  // - When an appropriate signal is received from the kernel about completion
  //   of SOP' Discovery.
  //
  // Since neither of the above have been implemented yet, we can call this
  // function explicitly for the sake of unit tests.
  void SetNumAltModes(int num_alt_modes) { num_alt_modes_ = num_alt_modes; }

  // Check if a particular alt mode index (as specified by the Type C connector
  // class framework) is registered.
  bool IsAltModePresent(int index);

  // Return the alternate modes with index |index|, and nullptr if such an
  // alternate modes doesn't exist.
  AltMode* GetAltMode(int index);

  // Check whether the cable supports Thunderbolt3 speed requirements.
  bool TBT3PDIdentityCheck();

 private:
  // Map representing all SOP' alternate modes.
  // The key is the index of the alternate mode as determined
  // by the connector class sysfs directory. For example,
  // an alternate mode which has the directory: "sys/class/port0-plug0.2" will
  // use a key of "2".
  std::map<int, std::unique_ptr<AltMode>> alt_modes_;
  int num_alt_modes_;
};

}  // namespace typecd

#endif  // TYPECD_CABLE_H_
