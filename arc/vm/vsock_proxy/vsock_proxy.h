// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARC_VM_VSOCK_PROXY_VSOCK_PROXY_H_
#define ARC_VM_VSOCK_PROXY_VSOCK_PROXY_H_

#include <stdint.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <base/files/file_descriptor_watcher_posix.h>
#include <base/files/scoped_file.h>
#include <base/macros.h>
#include <base/memory/weak_ptr.h>
#include <base/threading/thread.h>

#include "arc/vm/vsock_proxy/message.pb.h"

namespace base {
class FilePath;
}

namespace arc {

class LocalFile;
class ProxyFileSystem;

// Proxies between local file descriptors and given VSOCK socket by Message
// protocol.
class VSockProxy {
 public:
  // Represents whether this proxy is server (host) side one, or client (guest)
  // side one.
  enum class Type {
    SERVER = 1,
    CLIENT = 2,
  };

  class Delegate {
   public:
    virtual ~Delegate() = default;

    // Returns the type of this proxy.
    virtual Type GetType() const = 0;

    // Returns the file descriptor to watch for incoming messages.
    virtual int GetPollFd() = 0;

    // Creates a proxied file descriptor for the given handle.
    // Accessing the returned FD results in calling Pread(), Pwrite(), and
    // Fstat().
    virtual base::ScopedFD CreateProxiedRegularFile(int64_t handle,
                                                    int32_t flags) = 0;

    // Sends the message to the proxy process on the other side and returns true
    // on success.
    virtual bool SendMessage(const arc_proxy::VSockMessage& message,
                             const std::vector<base::ScopedFD>& fds) = 0;

    // Receives a message from the proxy process on the other side and returns
    // true on success.
    virtual bool ReceiveMessage(arc_proxy::VSockMessage* message,
                                std::vector<base::ScopedFD>* fds) = 0;

    // Called when the vsock proxy has stopped.
    virtual void OnStopped() = 0;
  };
  explicit VSockProxy(Delegate* delegate);
  ~VSockProxy();

  // Registers the |fd| whose type is |fd_type| to watch.
  // Internally, this creates an object to read/write Message protocol buffer.
  // If |handle| is the value corresponding to the file descriptor on
  // messages on VSOCK. If 0 is set, this internally generates the handle.
  // Returns handle or 0 on error.
  int64_t RegisterFileDescriptor(base::ScopedFD fd,
                                 arc_proxy::FileDescriptor::Type fd_type,
                                 int64_t handle);

  // Requests to connect(2) to a unix domain socket at |path| in the other
  // side.
  // |callback| will be called with errno, and the connected handle iff
  // succeeded.
  using ConnectCallback = base::OnceCallback<void(int, int64_t)>;
  void Connect(const base::FilePath& path, ConnectCallback callback);

  // Requests to call pread(2) for the file in the other side represented by
  // the |handle| with |count| and |offset|.
  // |callback| will be called with errno, and read blob iff succeeded.
  using PreadCallback = base::OnceCallback<void(int, const std::string&)>;
  void Pread(int64_t handle,
             uint64_t count,
             uint64_t offset,
             PreadCallback callback);

  // Requests to call pwrite(2) for the file in the other side represented by
  // the |handle| with |blob| and |offset|.
  // |callback| will be called with errno and the number of bytes written.
  using PwriteCallback = base::OnceCallback<void(int, int64_t)>;
  void Pwrite(int64_t handle,
              std::string blob,
              uint64_t offset,
              PwriteCallback callback);

  // Sends an event to close the given |handle| to the other side.
  void Close(int64_t handle);

  // Requests to call fstat(2) for the file in the other side represented by
  // the |handle|.
  // |callback| will be called with errno, and size if succeeded.
  using FstatCallback = base::OnceCallback<void(int, int64_t)>;
  void Fstat(int64_t handle, FstatCallback callback);

 private:
  // Callback called when VSOCK gets ready to read.
  // Reads Message from VSOCK file descriptor, and dispatches it to the
  // corresponding local file descriptor.
  void OnVSockReadReady();

  // Handles a message sent from the other side's proxy.
  bool HandleMessage(arc_proxy::VSockMessage* message,
                     std::vector<base::ScopedFD>* received_fds);

  // Stops this proxy.
  void Stop();

  // Handlers for each command.
  // TODO(crbug.com/842960): Use pass-by-value when protobuf is upreved enough
  // to support rvalues. (At least, 3.5, or maybe 3.6).
  bool OnClose(arc_proxy::Close* close);
  bool OnData(arc_proxy::Data* data, std::vector<base::ScopedFD>* received_fds);
  bool OnDataInternal(arc_proxy::Data* data);
  bool OnConnectRequest(arc_proxy::ConnectRequest* request);
  bool OnConnectResponse(arc_proxy::ConnectResponse* response);
  void OnPreadRequest(arc_proxy::PreadRequest* request);
  void SendPreadResponse(int64_t cookie, arc_proxy::PreadResponse response);
  bool OnPreadResponse(arc_proxy::PreadResponse* response);
  void OnPwriteRequest(arc_proxy::PwriteRequest* request);
  void SendPwriteResponse(int64_t cookie, arc_proxy::PwriteResponse response);
  bool OnPwriteResponse(arc_proxy::PwriteResponse* response);
  void OnFstatRequest(arc_proxy::FstatRequest* request);
  void SendFstatResponse(int64_t cookie, arc_proxy::FstatResponse response);
  bool OnFstatResponse(arc_proxy::FstatResponse* response);

  // Callback called when local file descriptor gets ready to read.
  // Reads Message from the file descriptor corresponding to the |handle|,
  // and forwards to VSOCK connection.
  void OnLocalFileDesciptorReadReady(int64_t handle);

  // Converts the given data to outgoing VSockMessage.
  bool ConvertDataToVSockMessage(std::string blob,
                                 std::vector<base::ScopedFD> fds,
                                 arc_proxy::VSockMessage* message,
                                 std::vector<base::ScopedFD>* fds_to_send);

  // Handles an error on a local file.
  void HandleLocalFileError(int64_t handle);

  // Returns a bland new cookie to start a sequence of operations.
  int64_t GenerateCookie();

  Delegate* delegate_;
  std::unique_ptr<base::FileDescriptorWatcher::Controller> message_watcher_;

  base::Thread blocking_task_thread_{"BlockingThread"};

  // Map from a |handle| (see message.proto for details) to a file
  // instance wrapping the file descriptor and its watcher.
  // Erasing the entry from this map should close the file descriptor
  // automatically, because file descriptor is owned by |file|.
  struct FileDescriptorInfo {
    // File instance to read/write Message.
    std::unique_ptr<LocalFile> file;

    // Controller of FileDescriptorWatcher. Destroying this will
    // stop watching.
    // This can be null, if there's no need to watch the file descriptor.
    std::unique_ptr<base::FileDescriptorWatcher::Controller> controller;
  };
  std::map<int64_t, FileDescriptorInfo> fd_map_;

  // For handle and cookie generation rules, please find the comment in
  // message.proto.
  int64_t next_handle_;
  int64_t next_cookie_;

  // Map from cookie to its pending callback.
  std::map<int64_t, ConnectCallback> pending_connect_;
  std::map<int64_t, PreadCallback> pending_pread_;
  std::map<int64_t, PwriteCallback> pending_pwrite_;
  std::map<int64_t, FstatCallback> pending_fstat_;

  // WeakPtrFactory needs to be declared as the member of the class, so that
  // on destruction, any pending Callbacks bound to WeakPtr are cancelled
  // first.
  base::WeakPtrFactory<VSockProxy> weak_factory_{this};

  DISALLOW_COPY_AND_ASSIGN(VSockProxy);
};

}  // namespace arc

#endif  // ARC_VM_VSOCK_PROXY_VSOCK_PROXY_H_
