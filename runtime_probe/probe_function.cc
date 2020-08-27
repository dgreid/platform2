// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runtime_probe/probe_function.h"

#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <vector>

#include <base/files/file_util.h>
#include <base/json/json_reader.h>
#include <base/json/json_writer.h>
#include <base/values.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/bus.h>
#include <dbus/message.h>
#include <dbus/object_proxy.h>

namespace runtime_probe {

namespace {

enum class PipeState {
  PENDING,
  ERROR,
  DONE,
};

// The system-defined size of buffer used to read from a pipe.
const size_t kBufferSize = PIPE_BUF;
// Seconds to wait for runtime_probe_helper to send probe results.
const time_t kWaitSeconds = 5;

PipeState ReadPipe(int src_fd, std::string* dst_str) {
  char buffer[kBufferSize];
  const ssize_t bytes_read = HANDLE_EINTR(read(src_fd, buffer, kBufferSize));
  if (bytes_read < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
    PLOG(ERROR) << "read() from fd " << src_fd << " failed";
    return PipeState::ERROR;
  }
  if (bytes_read == 0) {
    return PipeState::DONE;
  }
  if (bytes_read > 0) {
    dst_str->append(buffer, bytes_read);
  }
  return PipeState::PENDING;
}

bool ReadNonblockingPipeToString(int fd, std::string* out) {
  fd_set read_fds;
  struct timeval timeout;

  FD_ZERO(&read_fds);
  FD_SET(fd, &read_fds);

  timeout.tv_sec = kWaitSeconds;
  timeout.tv_usec = 0;

  while (true) {
    int retval = select(fd + 1, &read_fds, nullptr, nullptr, &timeout);
    if (retval < 0) {
      PLOG(ERROR) << "select() failed from runtime_probe_helper";
      return false;
    }

    // Should only happen on timeout. Log a warning here, so we get at least a
    // log if the process is stale.
    if (retval == 0) {
      LOG(WARNING) << "select() timed out. Process might be stale.";
      return false;
    }

    PipeState state = ReadPipe(fd, out);
    if (state == PipeState::DONE) {
      return true;
    }
    if (state == PipeState::ERROR) {
      return false;
    }
  }
}

}  // namespace

using DataType = typename ProbeFunction::DataType;

std::unique_ptr<ProbeFunction> ProbeFunction::FromValue(const base::Value& dv) {
  if (!dv.is_dict()) {
    LOG(ERROR) << "ProbeFunction::FromValue takes a dictionary as parameter";
    return nullptr;
  }

  if (dv.DictSize() == 0) {
    LOG(ERROR) << "No function name found in the ProbeFunction dictionary";
    return nullptr;
  }

  if (dv.DictSize() > 1) {
    LOG(ERROR) << "More than 1 function names specified in the ProbeFunction"
                  " dictionary";
    return nullptr;
  }

  const auto& it = dv.DictItems().begin();

  // function_name is the only key exists in the dictionary */
  const auto& function_name = it->first;
  const auto& kwargs = it->second;

  if (registered_functions_.find(function_name) ==
      registered_functions_.end()) {
    // TODO(stimim): Should report an error.
    LOG(ERROR) << "Function \"" << function_name << "\" not found";
    return nullptr;
  }

  if (!kwargs.is_dict()) {
    // TODO(stimim): implement syntax sugar.
    LOG(ERROR) << "Function argument should be a dictionary";
    return nullptr;
  }

  std::unique_ptr<ProbeFunction> ret_value =
      registered_functions_[function_name](kwargs);
  ret_value->raw_value_ = dv.Clone();

  return ret_value;
}

constexpr auto kDebugdRunProbeHelperMethodName = "EvaluateProbeFunction";
constexpr auto kDebugdRunProbeHelperDefaultTimeoutMs = 10 * 1000;  // in ms

bool ProbeFunction::InvokeHelper(std::string* result) const {
  std::string tmp_json_string;
  CHECK(raw_value_.has_value());
  base::JSONWriter::Write(*raw_value_, &tmp_json_string);

  dbus::Bus::Options ops;
  ops.bus_type = dbus::Bus::SYSTEM;
  scoped_refptr<dbus::Bus> bus(new dbus::Bus(std::move(ops)));

  if (!bus->Connect()) {
    LOG(ERROR) << "Failed to connect to system D-Bus service.";
    return false;
  }

  dbus::ObjectProxy* object_proxy = bus->GetObjectProxy(
      debugd::kDebugdServiceName, dbus::ObjectPath(debugd::kDebugdServicePath));

  dbus::MethodCall method_call(debugd::kDebugdInterface,
                               kDebugdRunProbeHelperMethodName);
  dbus::MessageWriter writer(&method_call);

  writer.AppendString(GetFunctionName());
  writer.AppendString(tmp_json_string);

  std::unique_ptr<dbus::Response> response = object_proxy->CallMethodAndBlock(
      &method_call, kDebugdRunProbeHelperDefaultTimeoutMs);
  if (!response) {
    LOG(ERROR) << "Failed to issue D-Bus call to method "
               << kDebugdRunProbeHelperMethodName
               << " of debugd D-Bus interface.";
    return false;
  }

  dbus::MessageReader reader(response.get());
  base::ScopedFD read_fd{};
  if (!reader.PopFileDescriptor(&read_fd)) {
    LOG(ERROR) << "Failed to read fd that represents the read end of the pipe"
                  " from debugd.";
    return false;
  }
  if (!ReadNonblockingPipeToString(read_fd.get(), result)) {
    LOG(ERROR) << "Cannot read result from helper";
    return false;
  }
  return true;
}

base::Optional<base::Value> ProbeFunction::InvokeHelperToJSON() const {
  std::string raw_output;
  if (!InvokeHelper(&raw_output)) {
    return base::nullopt;
  }
  return base::JSONReader::Read(raw_output);
}

int ProbeFunction::EvalInHelper(std::string* output) const {
  return 0;
}

}  // namespace runtime_probe
