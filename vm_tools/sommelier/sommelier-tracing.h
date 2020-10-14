// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_SOMMELIER_SOMMELIER_TRACING_H_
#define VM_TOOLS_SOMMELIER_SOMMELIER_TRACING_H_

#if defined(PERFETTO_TRACING)
#include <perfetto.h>

PERFETTO_DEFINE_CATEGORIES(perfetto::Category("surface").SetDescription(
    "Events for Wayland surface management"));
#else
#define TRACE_EVENT(category, name)
#endif

void initialize_tracing();
void enable_tracing();
void dump_trace(char const* filename);

#endif  // VM_TOOLS_SOMMELIER_SOMMELIER_TRACING_H_
