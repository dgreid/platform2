// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef VM_TOOLS_SOMMELIER_VIRTUALIZATION_WAYLAND_CHANNEL_H_
#define VM_TOOLS_SOMMELIER_VIRTUALIZATION_WAYLAND_CHANNEL_H_

#include <cstdint>

/*
 * Copied from `VIRTWL_SEND_MAX_ALLOCS`.  It was originally set this way
 * because it seemed like a reasonable limit.
 */
#define WAYLAND_MAX_FDs 28

struct WaylandSendReceive {
  int socket_fd;

  int fds[WAYLAND_MAX_FDs];
  uint32_t num_fds;
  uint8_t* data;
  size_t data_size;
};

struct WaylandBufferCreateInfo {
  /*
   * If true, create a dmabuf on the host.  If not, create a shared memory
   * region.  A dmabuf can be scanned out by the display engine directly,
   * enabling zero copy.  A shared memory region necessitates a copy to a
   * dma-buf by the host compositor.
   */
  bool dmabuf;

  /*
   * dma-buf parameters.  The allocation is done by host minigbm and used when
   * crosvm is built with the "wl-dmabuf" feature and virtgpu 3d is not
   * enabled.  The modifier is not present, because we only want to allocate
   * linear zero-copy buffers in this case.  The modifier makes sense when
   * virtgpu 3d is enabled, but in that case guest Mesa gbm (backed by Virgl)
   * allocates the resource, not sommelier.
   */
  uint32_t width;
  uint32_t height;
  uint32_t drm_format;

  /*
   * Shared memory region parameters.  The allocation is done by memfd(..) on
   * the host.
   */
  uint32_t size;
};

/*
 * Linux mode-setting APIs [drmModeAddFB2(..)] and Wayland normally specify
 * four planes, even though three are used in practice.  Follow that convention
 * here.
 */
struct WaylandBufferCreateOutput {
  int fd;
  uint32_t offsets[4];
  uint32_t strides[4];
};

class WaylandChannel {
 public:
  WaylandChannel() {}
  virtual ~WaylandChannel() {}

  // Initializes the Wayland Channel.  Returns 0 on success, -errno on failure.
  virtual int32_t init() = 0;

  // Creates a new context for handling the wayland command stream.  Returns 0
  // on success, and a pollable `out_socket_fd`.  This fd represents the
  // connection to the host compositor, and used for subsequent `send` and
  // `receive` operations.
  //
  // Returns -errno on failure.
  virtual int32_t create_context(int* out_socket_fd) = 0;

  // Creates a new clipboard pipe for Wayland input.  Note this interface can't
  // wrap a call to "pipe", and is named based on VIRTWL_IOCTL_NEW_PIPE.  A new
  // interface may be designed in the future.
  //
  // Returns 0 on success, and a readable `out_pipe_fd`.
  // Returns -errno on failure.
  virtual int32_t create_pipe(int* out_pipe_fd) = 0;

  // Sends fds and associated commands to the host [like sendmsg(..)].  The fds
  // are converted to host handles using an implementation specific method.
  // For virtwl, either:
  //  (a) virtwl allocated resources are sent.
  //  (b) The virtgpu resource handle is fished out by virtwl.
  //
  // Returns 0 on success.  Returns -errno on failure.  If `send.data_size` is
  // than greater zero, then the caller must provide a pointer to valid memory
  // in `send.data`.
  virtual int32_t send(const struct WaylandSendReceive& send) = 0;

  // Receives fds and associated commands from the host [like recvmsg(..)].
  // The use cases for receiving fds are:
  //
  // (a) wayland pipes, which are forwarded from the host to the guest
  // (b) release fences from the compositor
  //
  // virtwl supports (a), and support for (b) in Linux may take some time
  // [https://lwn.net/Articles/814587/].  ChromeOS already has support at the
  // kernel mode setting level for release fences.  It has yet to be plumbed
  // at the host compositor level.
  //
  // Returns 0 on success.  Returns -errno on failure.  If the returned
  // `receive.data_size` is than greater zero, then the caller takes ownership
  // of `receive.data` and must free(..) the memory when appropriate.
  virtual int32_t receive(struct WaylandSendReceive& receive) = 0;

  // Allocates a shared memory resource or dma-buf on the host.  Maps it into
  // the guest.  The intended use case for this function is sharing resources
  // with the host compositor when virtgpu 3d is not enabled.
  //
  // Returns 0 on success.  Returns -errno on success.
  virtual int32_t allocate(const struct WaylandBufferCreateInfo& create_info,
                           struct WaylandBufferCreateOutput& create_output) = 0;

  // Synchronizes accesses to previously created host dma-buf.
  // Returns 0 on success.  Returns -errno on failure.
  virtual int32_t sync(int dmabuf_fd, uint64_t flags) = 0;
};

class VirtWaylandChannel : public WaylandChannel {
 public:
  VirtWaylandChannel() : virtwl_{-1} {}
  ~VirtWaylandChannel();

  int32_t init() override;
  int32_t create_context(int* out_socket_fd) override;
  int32_t create_pipe(int* out_pipe_fd) override;
  int32_t send(const struct WaylandSendReceive& send) override;
  int32_t receive(struct WaylandSendReceive& receive) override;

  int32_t allocate(const struct WaylandBufferCreateInfo& create_info,
                   struct WaylandBufferCreateOutput& create_output) override;

  int32_t sync(int dmabuf_fd, uint64_t flags) override;

 private:
  // virtwl device file descriptor
  int32_t virtwl_;
};

#endif  // VM_TOOLS_SOMMELIER_VIRTUALIZATION_WAYLAND_CHANNEL_H_
