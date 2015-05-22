// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBCHROMEOS_CHROMEOS_ASYNCHRONOUS_SIGNAL_HANDLER_H_
#define LIBCHROMEOS_CHROMEOS_ASYNCHRONOUS_SIGNAL_HANDLER_H_

#include <signal.h>
#include <sys/signalfd.h>

#include <map>

#include <base/callback.h>
#include <base/compiler_specific.h>
#include <base/macros.h>
#include <base/memory/scoped_ptr.h>
#include <base/message_loop/message_loop.h>
#include <chromeos/chromeos_export.h>

namespace chromeos {
// Sets up signal handlers for registered signals, and converts signal receipt
// into a write on a pipe. Watches that pipe for data and, when some appears,
// execute the associated callback.
class CHROMEOS_EXPORT AsynchronousSignalHandler
    : public base::MessageLoopForIO::Watcher {
 public:
  AsynchronousSignalHandler();
  ~AsynchronousSignalHandler() override;

  // The callback called when a signal is received.
  typedef base::Callback<bool(const struct signalfd_siginfo&)> SignalHandler;

  // Initialize the handler.
  void Init();

  // Register a new handler for the given |signal|, replacing any previously
  // registered handler. |callback| will be called on the thread the
  // |AsynchronousSignalHandler| is bound to when a signal is received. The
  // received |signalfd_siginfo| will be passed to |callback|. |callback| must
  // returns |true| if the signal handler must be unregistered, and |false|
  // otherwise. Due to an implementation detail, you cannot set any sigaction
  // flags you might be accustomed to using. This might matter if you hoped to
  // use SA_NOCLDSTOP to avoid getting a SIGCHLD when a child process receives a
  // SIGSTOP.
  void RegisterHandler(int signal, const SignalHandler& callback);

  // Unregister a previously registered handler for the given |signal|.
  void UnregisterHandler(int signal);

  // Implementation of base::MessageLoopForIO::Watcher
  void OnFileCanReadWithoutBlocking(int fd) override;
  void OnFileCanWriteWithoutBlocking(int fd) override;

 private:
  // Controller used to manage watching of signalling pipe.
  scoped_ptr<base::MessageLoopForIO::FileDescriptorWatcher> fd_watcher_;

  // The registered callbacks.
  typedef std::map<int, SignalHandler> Callbacks;
  Callbacks registered_callbacks_;

  // File descriptor for accepting signals indicated by |signal_mask_|.
  int descriptor_;

  // A set of signals to be handled after the dispatcher is running.
  sigset_t signal_mask_;

  // A copy of the signal mask before the dispatcher starts, which will be
  // used to restore to the original state when the dispatcher stops.
  sigset_t saved_signal_mask_;

  // Resets the given signal to its default behavior. Doesn't touch
  // |registered_callbacks_|.
  CHROMEOS_PRIVATE void ResetSignal(int signal);

  // Updates the set of signals that this handler listens to.
  CHROMEOS_PRIVATE void UpdateSignals();

  DISALLOW_COPY_AND_ASSIGN(AsynchronousSignalHandler);
};

}  // namespace chromeos

#endif  // LIBCHROMEOS_CHROMEOS_ASYNCHRONOUS_SIGNAL_HANDLER_H_
