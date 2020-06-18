// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MIST_EVENT_DISPATCHER_H_
#define MIST_EVENT_DISPATCHER_H_

#include <map>
#include <memory>

#include <base/callback.h>
#include <base/files/file_descriptor_watcher_posix.h>
#include <base/macros.h>
#include <base/memory/ref_counted.h>
#include <base/message_loop/message_loop.h>
#include <base/time/time.h>

namespace mist {

// An event dispatcher for posting a task to a message loop, and for monitoring
// when a file descriptor is ready for I/O. To allow file descriptor monitoring
// via libevent, base::MessageLoopForIO, which uses base::MessagePumpLibevent,
// is used as the underlying message loop.
class EventDispatcher {
 public:
  enum class Mode {
    READ,
    WRITE,
    READ_WRITE,
  };

  EventDispatcher();
  ~EventDispatcher();

  // Starts dispatching event in a blocking manner until Stop() is called.
  void DispatchForever();

  // Stop dispatching events.
  void Stop();

  // Posts |task| to the message loop for execution. Returns true on success.
  bool PostTask(const base::Closure& task);

  // Posts |task| to the message loop for execution after the specified |delay|.
  // Returns true on success.
  bool PostDelayedTask(const base::Closure& task, const base::TimeDelta& delay);

  // Starts watching |file_descriptor| for its readiness for I/O based on |mode|
  // |callback| is invoked when |file_descriptor| is ready for I/O. Returns true
  // on success.
  bool StartWatchingFileDescriptor(int file_descriptor,
                                   Mode mode,
                                   const base::RepeatingClosure& callback);

  // Stops watching |file_descriptor| for its readiness for I/O. Returns true on
  // success.
  bool StopWatchingFileDescriptor(int file_descriptor);

  // Stops watching all file descriptors that have been watched via
  // StartWatchingFileDescriptor(). Returns true on success.
  void StopWatchingAllFileDescriptors();

 private:
  struct Watcher {
    std::unique_ptr<base::FileDescriptorWatcher::Controller> read_watcher;
    std::unique_ptr<base::FileDescriptorWatcher::Controller> write_watcher;
  };

  base::MessageLoopForIO message_loop_;  // Do not use this directly.
  scoped_refptr<base::SingleThreadTaskRunner> task_runner_;
  base::OnceClosure quit_closure_;
  base::FileDescriptorWatcher watcher_{message_loop_.task_runner()};
  std::map<int, Watcher> file_descriptor_watchers_;

  DISALLOW_COPY_AND_ASSIGN(EventDispatcher);
};

}  // namespace mist

#endif  // MIST_EVENT_DISPATCHER_H_
