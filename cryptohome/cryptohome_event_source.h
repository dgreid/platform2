// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// CryptohomeEventSource - Implements a GSource for the glib main event loop.
// This class is used to marshal asynchronous mount results from the worker
// thread (see service.h/.cc) over to the main event loop.  That way, all of the
// D-Bus messages are received and sent from the one thread, ensuring that
// signals returned by asynchronous commands are serialized with the original
// call.
//
// CryptohomeEventSource uses a pipe(2) to implement a file descriptor-based
// GSource.  When events are added to the object, it writes a byte to the write
// side of the pipe.  Glib, in the main event loop, will test if the read side
// has data pending.  If so, it will call the dispatch method on the main
// thread.
//
// CryptohomeEventSourceSink is an interface that is implemented by Service.  It
// provides the implementation of the handler that is called on the main event
// loop when an event is processed.

#ifndef CRYPTOHOME_CRYPTOHOME_EVENT_SOURCE_H_
#define CRYPTOHOME_CRYPTOHOME_EVENT_SOURCE_H_

#include <base/synchronization/lock.h>
#include <dbus/dbus-glib.h>
#include <glib-object.h>
#include <memory>
#include <vector>

namespace cryptohome {

class CryptohomeEventBase;
class CryptohomeEventSourceSink;

class CryptohomeEventSource {
 public:
  CryptohomeEventSource();
  CryptohomeEventSource(const CryptohomeEventSource&) = delete;
  CryptohomeEventSource& operator=(const CryptohomeEventSource&) = delete;

  virtual ~CryptohomeEventSource();

  // Resets the event queue and re-attaches this GSource to the GMainContext.
  //
  // Parameters
  //   sink - Pointer to the handler called when events are dispatched
  //   main_context - The GMainContext to attach this GSource to
  void Reset(CryptohomeEventSourceSink* sink, GMainContext* main_context);

  // Returns whether or not there are events in the queue
  bool EventsPending();

  // Processes pending events in the queue
  void HandleDispatch();

  // Adds an event to the queue for processing.
  //
  // Parameters
  //   task_result - The event to add
  void AddEvent(std::unique_ptr<CryptohomeEventBase> event);

  // Clears all pending events from the queue
  void Clear();

 private:
  // Structure that glib provides in calls to the static handlers, allows
  // getting the instance of this CryptohomeEventSource.
  struct Source : public GSource {
    CryptohomeEventSource* event_source;
    GPollFD poll_fd;
  };

  struct SourceDeleter {
    void operator()(Source* source);
  };

  // Called by glib (see GSourceFuncs in the glib documentation)
  static gboolean Prepare(GSource* source, gint* timeout_ms);

  // Called by glib (see GSourceFuncs in the glib documentation)
  static gboolean Check(GSource* source);

  // Called by glib (see GSourceFuncs in the glib documentation)
  static gboolean Dispatch(GSource* source,
                           GSourceFunc unused_func,
                           gpointer unused_data);

  // The event sink that handles event notifications
  CryptohomeEventSourceSink* sink_ = nullptr;

  // The D-Bus GSource that we provide
  std::unique_ptr<Source, SourceDeleter> source_;

  // Pending events vector
  std::vector<std::unique_ptr<CryptohomeEventBase>> events_;

  // Used to provide thread-safe access to events_
  base::Lock events_lock_;

  // Structure initialized to the static callbacks above
  static GSourceFuncs source_functions_;

  // The pipe used for our GPollFD
  int pipe_fds_[2];
};

class CryptohomeEventBase {
 public:
  CryptohomeEventBase() = default;
  virtual ~CryptohomeEventBase() = default;

  virtual const char* GetEventName() const = 0;
};

class CryptohomeEventSourceSink {
 public:
  virtual ~CryptohomeEventSourceSink() = default;
  virtual void NotifyEvent(CryptohomeEventBase* event) = 0;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_CRYPTOHOME_EVENT_SOURCE_H_
