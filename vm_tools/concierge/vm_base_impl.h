// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_VM_BASE_IMPL_H_
#define VM_TOOLS_CONCIERGE_VM_BASE_IMPL_H_

#include "vm_tools/concierge/vm_interface.h"

#include <memory>
#include <utility>

#include <base/macros.h>
#include <base/files/scoped_temp_dir.h>
#include <brillo/process/process.h>
#include <chromeos/patchpanel/dbus/client.h>

namespace patchpanel {
class Client;
}

namespace vm_tools {
namespace concierge {

// A base class implementing common features that are shared between ArcVm,
// PluginVm and TerminaVm
class VmBaseImpl : public VmInterface {
 public:
  explicit VmBaseImpl(std::unique_ptr<patchpanel::Client> network_client)
      : network_client_(std::move(network_client)) {}

  // The pid of the child process.
  pid_t pid() { return process_.pid(); }

 protected:
  // Adjusts the amount of CPU the VM processes are allowed to use.
  static bool SetVmCpuRestriction(CpuRestrictionState cpu_restriction_state,
                                  const char* CpuCgroup);

  // DBus client for the networking service.
  std::unique_ptr<patchpanel::Client> network_client_;

  // Runtime directory for this VM.
  base::ScopedTempDir runtime_dir_;

  // Handle to the VM process.
  brillo::ProcessImpl process_;

  DISALLOW_COPY_AND_ASSIGN(VmBaseImpl);
};

}  // namespace concierge
}  // namespace vm_tools

#endif  // VM_TOOLS_CONCIERGE_VM_BASE_IMPL_H_
