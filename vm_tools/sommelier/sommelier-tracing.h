// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_SOMMELIER_SOMMELIER_TRACING_H_
#define VM_TOOLS_SOMMELIER_SOMMELIER_TRACING_H_

#if defined(PERFETTO_TRACING)
#include <perfetto.h>

PERFETTO_DEFINE_CATEGORIES(
    perfetto::Category("surface").SetDescription(
        "Events for Wayland surface management"),
    perfetto::Category("display").SetDescription("Events for Wayland display"),
    perfetto::Category("shell").SetDescription("Events for Wayland shell"),
    perfetto::Category("shm").SetDescription(
        "Events for Wayland shared memory"),
    perfetto::Category("viewport")
        .SetDescription("Events for Wayland viewport"),
    perfetto::Category("sync").SetDescription("Events for Wayland sync points"),
    perfetto::Category("other").SetDescription("Uncategorized Wayland calls."));
#else
#define TRACE_EVENT(category, name, ...)
#endif

void initialize_tracing(bool in_process_backend, bool system_backend);
void enable_tracing(bool create_session);
void dump_trace(char const* filename);

#endif  // VM_TOOLS_SOMMELIER_SOMMELIER_TRACING_H_
