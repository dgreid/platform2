// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sommelier-tracing.h"  // NOLINT(build/include_directory)

#include <assert.h>
#include <fcntl.h>
#include <memory>
#include <sys/stat.h>
#include <sys/types.h>
#include <vector>

#if defined(PERFETTO_TRACING)
PERFETTO_TRACK_EVENT_STATIC_STORAGE();

std::unique_ptr<perfetto::TracingSession> tracing_session;

void initialize_tracing(bool in_process_backend, bool system_backend) {
  perfetto::TracingInitArgs args;
  if (in_process_backend) {
    args.backends |= perfetto::kInProcessBackend;
  }
  if (system_backend) {
    args.backends |= perfetto::kSystemBackend;
  }

  perfetto::Tracing::Initialize(args);
  perfetto::TrackEvent::Register();
}

void enable_tracing(bool create_session) {
  perfetto::TraceConfig cfg;
  cfg.add_buffers()->set_size_kb(1024);  // Record up to 1 MiB.
  auto* ds_cfg = cfg.add_data_sources()->mutable_config();
  ds_cfg->set_name("track_event");

  if (create_session) {
    tracing_session = perfetto::Tracing::NewTrace();
    tracing_session->Setup(cfg);
    tracing_session->StartBlocking();
  }
}

void dump_trace(const char* trace_filename) {
  if (!trace_filename || !*trace_filename || !tracing_session) {
    return;
  }

  std::vector<char> trace_data(tracing_session->ReadTraceBlocking());

  // Write the trace into a file.
  int fd = open(trace_filename, O_RDWR | O_CREAT | O_TRUNC, 0644);
  if (fd == -1) {
    fprintf(stderr, "error: unable to open trace file %s: %s\n",
            trace_filename, strerror(errno));
    return;
  }
  size_t pos = 0;
  do {
    ssize_t ret = write(fd, &trace_data[pos], trace_data.size() - pos);
    if (ret < 0) {
      if (errno == EINTR || errno == EAGAIN) {
        continue;
      }
      fprintf(stderr, "error: unable to write trace file %s: %s\n",
              trace_filename, strerror(errno));
      close(fd);
      return;
    }
    pos += ret;
  } while (pos < trace_data.size());
  close(fd);
}

#else

// Stubs.

void initialize_tracing(bool in_process_backend, bool system_backend) {}

void enable_tracing(bool create_session) {}

void dump_trace(const char* trace_filename) {}

#endif
