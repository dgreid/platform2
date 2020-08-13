// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARC_VM_FORWARD_PSTORE_SERVICE_H_
#define ARC_VM_FORWARD_PSTORE_SERVICE_H_

#include <string>

#include <base/callback_forward.h>
#include <base/memory/scoped_refptr.h>
#include <base/memory/weak_ptr.h>
#include <base/timer/timer.h>
#include <brillo/files/safe_fd.h>
#include <dbus/bus.h>
#include <dbus/message.h>

namespace arc {

class Service {
 public:
  explicit Service(base::Closure quit_closure);
  Service(const Service&) = delete;
  Service& operator=(const Service&) = delete;
  ~Service();

  void Start();

 private:
  void OnSignalConnected(const std::string& interface_name,
                         const std::string& signal_name,
                         bool is_connected);
  void OnVmStoppedSignal(dbus::Signal* signal);
  void OnVmIdChangedSignal(dbus::Signal* signal);

  void ForwardPstore(const std::string& owner_id);
  void ForwardContents(const std::string& owner_id);

  scoped_refptr<dbus::Bus> bus_;
  brillo::SafeFD root_fd_;
  brillo::SafeFD pstore_fd_;
  brillo::SafeFD dest_fd_;
  base::Closure quit_closure_;
  base::RepeatingTimer timer_;
  base::WeakPtrFactory<Service> weak_ptr_factory_;
};

}  // namespace arc

#endif  // ARC_VM_FORWARD_PSTORE_SERVICE_H_
