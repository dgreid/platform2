// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef CRYPTOHOME_STATEFUL_RECOVERY_H_
#define CRYPTOHOME_STATEFUL_RECOVERY_H_

#include <string>

#include <base/callback.h>
#include <base/files/file_path.h>

namespace cryptohome {

class Platform;
class Service;

// This class handles recovery of encrypted data from the stateful partition.
// At present, it provides a simple way to export the encrypted data while the
// feature is maturing by checking for the existence of a file on the
// unencrypted portion of stateful.
//
// Once the feature has seen satisfactory airtime and all
// related tooling is robust, this class will implement a tighter mechanism
// for recovering the encrypted data in stateful that requires physical device
// modification or device owner modification:
//   http://crosbug.com/34219
//
class StatefulRecovery {
 public:
  // MountFunction is a function prototype that will mount the cryptohome for
  // the specified username and passkey. If the mounting is successful, true is
  // returned and out_home_path is set to the home path for the home directory
  // for the target user. Otherwise, false is returned and out_home_path is
  // unchanged. Note that it is the responsibility of the function to log any
  // detailed error should any arises.
  typedef base::Callback<bool(const std::string& username,
                              const std::string& passkey,
                              base::FilePath* out_home_path)>
      MountFunction;

  // UnmountFunction is a function that unmount all cryptohome. It returns true
  // on success and false on failure. Note that it is the responsibility of the
  // function to log any detailed error should any arises.
  typedef base::Callback<bool()> UnmountFunction;

  // IsOwnerFunction is a function that returns true if the given user is the
  // owner, and false otherwise.
  typedef base::Callback<bool(const std::string& username)> IsOwnerFunction;

  explicit StatefulRecovery(Platform* platform,
                            MountFunction mountfn,
                            UnmountFunction unmountfn,
                            IsOwnerFunction isownerfn);
  virtual ~StatefulRecovery() = default;

  // Returns true if recovery was requested by the device user.
  virtual bool Requested();
  // Returns true if it successfully recovered stateful contents.
  virtual bool Recover();
  // On Chrome hardware, sets the recovery request field and reboots.
  virtual void PerformReboot();

  static const char kRecoverSource[];
  static const char kRecoverDestination[];
  static const char kRecoverBlockUsage[];
  static const char kRecoverFilesystemDetails[];
  static const char kFlagFile[];

 private:
  // Returns true if a flag file indicating a recovery request exists and
  // contains the expected content.
  bool ParseFlagFile();

  // Copies encrypted partition details to recovery directory.
  bool CopyPartitionInfo();

  // Copies encrypted partition contents to recovery directory.
  bool CopyPartitionContents();

  // Copies the mounted user directory to recovery directory.
  bool CopyUserContents();

  // Versions of the recovery handler.
  bool RecoverV1();
  bool RecoverV2();

  bool requested_;
  Platform *platform_;
  MountFunction mountfn_;
  UnmountFunction unmountfn_;
  IsOwnerFunction isownerfn_;
  std::string version_;
  std::string user_;
  std::string passkey_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_STATEFUL_RECOVERY_H_
