// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ATTESTATION_PCA_AGENT_CLIENT_PROXY_FACTORY_H_
#define ATTESTATION_PCA_AGENT_CLIENT_PROXY_FACTORY_H_

#include <memory>

#include <attestation/proto_bindings/pca_agent.pb.h>
#include <base/memory/ref_counted.h>
#include <dbus/bus.h>

#include "attestation/pca_agent/dbus-proxies.h"

namespace attestation {
namespace pca_agent {
namespace client {

template <typename SequencedTaskRunnerType>
std::unique_ptr<org::chromium::PcaAgentProxyInterface> CreateWithDBusTaskRunner(
    const scoped_refptr<SequencedTaskRunnerType>& task_runner) {
  dbus::Bus::Options options;
  options.bus_type = dbus::Bus::SYSTEM;
  options.dbus_task_runner = task_runner;
  scoped_refptr<dbus::Bus> bus(new dbus::Bus(options));
  return std::make_unique<org::chromium::PcaAgentProxy>(bus);
}

}  // namespace client
}  // namespace pca_agent
}  // namespace attestation

#endif  // ATTESTATION_PCA_AGENT_CLIENT_PROXY_FACTORY_H_
