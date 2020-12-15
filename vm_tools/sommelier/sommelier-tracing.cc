// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sommelier-tracing.h"  // NOLINT(build/include_directory)

#include <assert.h>
#include <fcntl.h>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <vector>
#include <xcb/xproto.h>

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
    fprintf(stderr, "error: unable to open trace file %s: %s\n", trace_filename,
            strerror(errno));
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

static const char* xcb_atom_to_string(uint32_t atom) {
  switch (atom) {
    case XCB_ATOM_NONE:
      return "XCB_ATOM_NONE";
    case XCB_ATOM_PRIMARY:
      return "XCB_ATOM_PRIMARY";
    case XCB_ATOM_SECONDARY:
      return "XCB_ATOM_SECONDARY";
    case XCB_ATOM_ARC:
      return "XCB_ATOM_ARC";
    case XCB_ATOM_ATOM:
      return "XCB_ATOM_ATOM";
    case XCB_ATOM_BITMAP:
      return "XCB_ATOM_BITMAP";
    case XCB_ATOM_CARDINAL:
      return "XCB_ATOM_CARDINAL";
    case XCB_ATOM_COLORMAP:
      return "XCB_ATOM_COLORMAP";
    case XCB_ATOM_CURSOR:
      return "XCB_ATOM_CURSOR";
    case XCB_ATOM_CUT_BUFFER0:
      return "XCB_ATOM_CUT_BUFFER0";
    case XCB_ATOM_CUT_BUFFER1:
      return "XCB_ATOM_CUT_BUFFER1";
    case XCB_ATOM_CUT_BUFFER2:
      return "XCB_ATOM_CUT_BUFFER2";
    case XCB_ATOM_CUT_BUFFER3:
      return "XCB_ATOM_CUT_BUFFER3";
    case XCB_ATOM_CUT_BUFFER4:
      return "XCB_ATOM_CUT_BUFFER4";
    case XCB_ATOM_CUT_BUFFER5:
      return "XCB_ATOM_CUT_BUFFER5";
    case XCB_ATOM_CUT_BUFFER6:
      return "XCB_ATOM_CUT_BUFFER6";
    case XCB_ATOM_CUT_BUFFER7:
      return "XCB_ATOM_CUT_BUFFER7";
    case XCB_ATOM_DRAWABLE:
      return "XCB_ATOM_DRAWABLE";
    case XCB_ATOM_FONT:
      return "XCB_ATOM_FONT";
    case XCB_ATOM_INTEGER:
      return "XCB_ATOM_INTEGER";
    case XCB_ATOM_PIXMAP:
      return "XCB_ATOM_PIXMAP";
    case XCB_ATOM_POINT:
      return "XCB_ATOM_POINT";
    case XCB_ATOM_RECTANGLE:
      return "XCB_ATOM_RECTANGLE";
    case XCB_ATOM_RESOURCE_MANAGER:
      return "XCB_ATOM_RESOURCE_MANAGER";
    case XCB_ATOM_RGB_COLOR_MAP:
      return "XCB_ATOM_RGB_COLOR_MAP";
    case XCB_ATOM_RGB_BEST_MAP:
      return "XCB_ATOM_RGB_BEST_MAP";
    case XCB_ATOM_RGB_BLUE_MAP:
      return "XCB_ATOM_RGB_BLUE_MAP";
    case XCB_ATOM_RGB_DEFAULT_MAP:
      return "XCB_ATOM_RGB_DEFAULT_MAP";
    case XCB_ATOM_RGB_GRAY_MAP:
      return "XCB_ATOM_RGB_GRAY_MAP";
    case XCB_ATOM_RGB_GREEN_MAP:
      return "XCB_ATOM_RGB_GREEN_MAP";
    case XCB_ATOM_RGB_RED_MAP:
      return "XCB_ATOM_RGB_RED_MAP";
    case XCB_ATOM_STRING:
      return "XCB_ATOM_STRING";
    case XCB_ATOM_VISUALID:
      return "XCB_ATOM_VISUALID";
    case XCB_ATOM_WINDOW:
      return "XCB_ATOM_WINDOW";
    case XCB_ATOM_WM_COMMAND:
      return "XCB_ATOM_WM_COMMAND";
    case XCB_ATOM_WM_HINTS:
      return "XCB_ATOM_WM_HINTS";
    case XCB_ATOM_WM_CLIENT_MACHINE:
      return "XCB_ATOM_WM_CLIENT_MACHINE";
    case XCB_ATOM_WM_ICON_NAME:
      return "XCB_ATOM_WM_ICON_NAME";
    case XCB_ATOM_WM_ICON_SIZE:
      return "XCB_ATOM_WM_ICON_SIZE";
    case XCB_ATOM_WM_NAME:
      return "XCB_ATOM_WM_NAME";
    case XCB_ATOM_WM_NORMAL_HINTS:
      return "XCB_ATOM_WM_NORMAL_HINTS";
    case XCB_ATOM_WM_SIZE_HINTS:
      return "XCB_ATOM_WM_SIZE_HINTS";
    case XCB_ATOM_WM_ZOOM_HINTS:
      return "XCB_ATOM_WM_ZOOM_HINTS";
    case XCB_ATOM_MIN_SPACE:
      return "XCB_ATOM_MIN_SPACE";
    case XCB_ATOM_NORM_SPACE:
      return "XCB_ATOM_NORM_SPACE";
    case XCB_ATOM_MAX_SPACE:
      return "XCB_ATOM_MAX_SPACE";
    case XCB_ATOM_END_SPACE:
      return "XCB_ATOM_END_SPACE";
    case XCB_ATOM_SUPERSCRIPT_X:
      return "XCB_ATOM_SUPERSCRIPT_X";
    case XCB_ATOM_SUPERSCRIPT_Y:
      return "XCB_ATOM_SUPERSCRIPT_Y";
    case XCB_ATOM_SUBSCRIPT_X:
      return "XCB_ATOM_SUBSCRIPT_X";
    case XCB_ATOM_SUBSCRIPT_Y:
      return "XCB_ATOM_SUBSCRIPT_Y";
    case XCB_ATOM_UNDERLINE_POSITION:
      return "XCB_ATOM_UNDERLINE_POSITION";
    case XCB_ATOM_UNDERLINE_THICKNESS:
      return "XCB_ATOM_UNDERLINE_THICKNESS";
    case XCB_ATOM_STRIKEOUT_ASCENT:
      return "XCB_ATOM_STRIKEOUT_ASCENT";
    case XCB_ATOM_STRIKEOUT_DESCENT:
      return "XCB_ATOM_STRIKEOUT_DESCENT";
    case XCB_ATOM_ITALIC_ANGLE:
      return "XCB_ATOM_ITALIC_ANGLE";
    case XCB_ATOM_X_HEIGHT:
      return "XCB_ATOM_X_HEIGHT";
    case XCB_ATOM_QUAD_WIDTH:
      return "XCB_ATOM_QUAD_WIDTH";
    case XCB_ATOM_WEIGHT:
      return "XCB_ATOM_WEIGHT";
    case XCB_ATOM_POINT_SIZE:
      return "XCB_ATOM_POINT_SIZE";
    case XCB_ATOM_RESOLUTION:
      return "XCB_ATOM_RESOLUTION";
    case XCB_ATOM_COPYRIGHT:
      return "XCB_ATOM_COPYRIGHT";
    case XCB_ATOM_NOTICE:
      return "XCB_ATOM_NOTICE";
    case XCB_ATOM_FONT_NAME:
      return "XCB_ATOM_FONT_NAME";
    case XCB_ATOM_FAMILY_NAME:
      return "XCB_ATOM_FAMILY_NAME";
    case XCB_ATOM_FULL_NAME:
      return "XCB_ATOM_FULL_NAME";
    case XCB_ATOM_CAP_HEIGHT:
      return "XCB_ATOM_CAP_HEIGHT";
    case XCB_ATOM_WM_CLASS:
      return "XCB_ATOM_WM_CLASS";
    case XCB_ATOM_WM_TRANSIENT_FOR:
      return "XCB_ATOM_WM_TRANSIENT_FOR";
    default:
      return "<unknown>";
  }
}

void perfetto_annotate_xcb_atom(const perfetto::EventContext& event,
                                const char* name,
                                xcb_atom_t atom_int) {
  auto* dbg = event.event()->add_debug_annotations();
  dbg->set_name(name);
  const char* atom = xcb_atom_to_string(atom_int);
  if (atom) {
    dbg->set_string_value(atom, strlen(atom));
  } else {
    static const std::string unknown("<unknown>");
    dbg->set_string_value(unknown);
  }
}

void perfetto_annotate_xcb_property_state(const perfetto::EventContext& event,
                                          const char* name,
                                          unsigned int state) {
  auto* dbg = event.event()->add_debug_annotations();
  dbg->set_name(name);
  if (state == XCB_PROPERTY_NEW_VALUE) {
    static const std::string prop_new("XCB_PROPERTY_NEW_VALUE");
    dbg->set_string_value(prop_new);
  } else if (state == XCB_PROPERTY_DELETE) {
    static const std::string prop_delete("XCB_PROPERTY_DELETE");
    dbg->set_string_value(prop_delete);
  } else {
    static const std::string unknown("<unknown>");
    dbg->set_string_value(unknown);
  }
}

#else

// Stubs.

void initialize_tracing(bool in_process_backend, bool system_backend) {}

void enable_tracing(bool create_session) {}

void dump_trace(const char* trace_filename) {}

#endif  // PERFETTO_TRACING
