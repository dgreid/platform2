// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "midis/client_tracker.h"

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>

#include <memory>
#include <string>
#include <utility>

#include <base/bind.h>
#include <base/location.h>
#include <brillo/message_loops/message_loop.h>
#include <mojo/core/embedder/embedder.h>
#include <mojo/public/cpp/bindings/binding.h>
#include <mojo/public/cpp/system/invitation.h>

namespace {

constexpr char kMidisPipe[] = "arc-midis-pipe";

// Implementation of the MidisHost interface. This is used to
// get the actual MidisManager interface which is used by the client to
// communicate with midis.
// A request to initialize this should be initiated by the ArcBridgeHost.
//
// NOTE: It is expected that this class should only be instantiated once
// during the lifetime of the service. An error in the Message Pipe associated
// with this class is most likely an unrecoverable error, and will necessitate
// the restart of the midis service from Chrome.
class MidisHostImpl : public arc::mojom::MidisHost {
 public:
  // |client_tracker|, which must outlive MidisHostImpl, is owned by the caller.
  MidisHostImpl(arc::mojom::MidisHostRequest request,
                midis::ClientTracker* client_tracker);
  MidisHostImpl(const MidisHostImpl&) = delete;
  MidisHostImpl& operator=(const MidisHostImpl&) = delete;

  ~MidisHostImpl() override = default;

  // mojom::MidisHost:
  void Connect(arc::mojom::MidisServerRequest request,
               arc::mojom::MidisClientPtr client_ptr) override;

 private:
  // It's safe to hold a raw pointer to ClientTracker since we can assume
  // that the lifecycle of ClientTracker is a superset of the lifecycle of
  // MidisHostImpl.
  midis::ClientTracker* client_tracker_;
  mojo::Binding<arc::mojom::MidisHost> binding_;
};

MidisHostImpl::MidisHostImpl(arc::mojom::MidisHostRequest request,
                             midis::ClientTracker* client_tracker)
    : client_tracker_(client_tracker), binding_(this, std::move(request)) {}

void MidisHostImpl::Connect(arc::mojom::MidisServerRequest request,
                            arc::mojom::MidisClientPtr client_ptr) {
  VLOG(1) << "Connect() called.";
  client_tracker_->MakeMojoClient(std::move(request), std::move(client_ptr));
}

}  // namespace

namespace midis {

void ClientTracker::MakeMojoClient(arc::mojom::MidisServerRequest request,
                                   arc::mojom::MidisClientPtr client_ptr) {
  client_id_counter_++;
  VLOG(1) << "MakeMojoClient called.";
  auto new_cli = std::make_unique<Client>(
      device_tracker_, client_id_counter_,
      base::Bind(&ClientTracker::RemoveClient, weak_factory_.GetWeakPtr()),
      std::move(request), std::move(client_ptr));

  if (new_cli) {
    clients_.emplace(client_id_counter_, std::move(new_cli));
  }
}

ClientTracker::ClientTracker() : client_id_counter_(0), weak_factory_(this) {}

ClientTracker::~ClientTracker() {
  for (auto& client : clients_) {
    device_tracker_->RemoveClientFromDevices(client.first);
  }
  clients_.clear();
  ipc_support_ = nullptr;
}

void ClientTracker::InitClientTracker() {
  VLOG(1) << "Start client Mojo server.";

  mojo::core::Init();
  ipc_support_ = std::make_unique<mojo::core::ScopedIPCSupport>(
      base::ThreadTaskRunnerHandle::Get(),
      mojo::core::ScopedIPCSupport::ShutdownPolicy::FAST);
}

void ClientTracker::RemoveClient(uint32_t client_id) {
  // First delete all references to this device.
  device_tracker_->RemoveClientFromDevices(client_id);
  clients_.erase(client_id);
}

void ClientTracker::AcceptProxyConnection(base::ScopedFD fd) {
  DCHECK(sequence_checker_.CalledOnValidSequence());
  mojo::IncomingInvitation invitation = mojo::IncomingInvitation::Accept(
      mojo::PlatformChannelEndpoint(mojo::PlatformHandle(std::move(fd))));
  mojo::ScopedMessagePipeHandle child_pipe =
      invitation.ExtractMessagePipe(kMidisPipe);
  midis_host_ = std::make_unique<MidisHostImpl>(
      mojo::InterfaceRequest<arc::mojom::MidisHost>(std::move(child_pipe)),
      this);
}

bool ClientTracker::IsProxyConnected() {
  return midis_host_ != nullptr;
}

}  // namespace midis
