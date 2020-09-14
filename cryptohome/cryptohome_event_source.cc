// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/cryptohome_event_source.h"

#include <poll.h>
#include <unistd.h>

#include <memory>
#include <utility>

#include <base/logging.h>

namespace cryptohome {

GSourceFuncs CryptohomeEventSource::source_functions_ = {
    CryptohomeEventSource::Prepare, CryptohomeEventSource::Check,
    CryptohomeEventSource::Dispatch, nullptr};

CryptohomeEventSource::CryptohomeEventSource() {
  pipe_fds_[0] = -1;
  pipe_fds_[1] = -1;
}

CryptohomeEventSource::~CryptohomeEventSource() {
  Clear();
}

void CryptohomeEventSource::Reset(CryptohomeEventSourceSink* sink,
                                  GMainContext* main_context) {
  sink_ = sink;
  source_.reset();

  for (int i = 0; i < 2; i++) {
    if (pipe_fds_[i] != -1) {
      close(pipe_fds_[i]);
      pipe_fds_[i] = -1;
    }
  }

  Clear();

  if (!pipe(pipe_fds_)) {
    source_.reset(
        static_cast<Source*>(g_source_new(&source_functions_, sizeof(Source))));
    source_->event_source = this;
    source_->poll_fd.fd = pipe_fds_[0];
    source_->poll_fd.events = G_IO_IN | G_IO_HUP | G_IO_ERR;
    source_->poll_fd.revents = 0;
    g_source_add_poll(source_.get(), &source_->poll_fd);
    if (main_context) {
      g_source_attach(source_.get(), main_context);
    }
    g_source_set_can_recurse(source_.get(), true);
  } else {
    LOG(ERROR) << "Couldn't set up pipe for notifications.";
  }
}

bool CryptohomeEventSource::EventsPending() {
  bool result = true;
  if (events_lock_.Try()) {
    result = !events_.empty();
    events_lock_.Release();
  }
  return result;
}

void CryptohomeEventSource::HandleDispatch() {
  // Clear pending notifications from the pipe.  This is done in reverse order
  // of the AddEvent() function so that we cannot get into a state where there
  // is an event queued but no pending read on the pipe.
  bool more = false;
  do {
    struct pollfd fds = {pipe_fds_[0], POLLIN, 0};
    if (poll(&fds, 1, 0) && (fds.revents & POLLIN)) {
      char c;
      if (read(pipe_fds_[0], &c, 1) == 1) {
        more = true;
      } else {
        more = false;
      }
    } else {
      more = false;
    }
  } while (more);

  // Now handle pending events.
  std::vector<std::unique_ptr<CryptohomeEventBase>> events;
  {
    base::AutoLock lock(events_lock_);
    events_.swap(events);
  }

  for (auto& event : events) {
    if (sink_) {
      sink_->NotifyEvent(event.get());
    }
  }
}

void CryptohomeEventSource::AddEvent(
    std::unique_ptr<CryptohomeEventBase> event) {
  base::AutoLock lock(events_lock_);
  events_.push_back(std::move(event));
  if (write(pipe_fds_[1], "G", 1) != 1) {
    LOG(INFO) << "Couldn't notify of pending events through the message pipe."
              << "  Events will be cleared on next call to Prepare().";
  }
}

void CryptohomeEventSource::Clear() {
  base::AutoLock lock(events_lock_);
  events_.clear();
}

void CryptohomeEventSource::SourceDeleter::operator()(Source* source) {
  g_source_destroy(source);
  g_source_unref(source);
}

gboolean CryptohomeEventSource::Prepare(GSource* source, gint* timeout_ms) {
  if (static_cast<Source*>(source)->event_source->EventsPending()) {
    *timeout_ms = 0;
    return true;
  }
  *timeout_ms = -1;
  return false;
}

gboolean CryptohomeEventSource::Check(GSource* source) {
  return static_cast<Source*>(source)->event_source->EventsPending();
}

gboolean CryptohomeEventSource::Dispatch(GSource* source,
                                         GSourceFunc unused_func,
                                         gpointer unused_data) {
  static_cast<Source*>(source)->event_source->HandleDispatch();
  return true;
}

}  // namespace cryptohome
