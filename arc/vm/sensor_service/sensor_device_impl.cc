// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/vm/sensor_service/sensor_device_impl.h"

#include <fcntl.h>
#include <unistd.h>

#include <utility>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/posix/eintr_wrapper.h>
#include <base/strings/string_util.h>
#include <mojo/public/cpp/system/platform_handle.h>

namespace arc {

namespace {

// Returns the path of the specified attribute under |iio_sysfs_dir|.
base::FilePath GetAttributePath(const base::FilePath& iio_sysfs_dir,
                                const std::string& name) {
  base::FilePath path = iio_sysfs_dir.Append(name);
  if (!path.IsAbsolute() || path.ReferencesParent()) {
    LOG(ERROR) << "Invalid path: " << path.value();
    return {};
  }
  return path;
}

}  // namespace

SensorDeviceImpl::SensorDeviceImpl(const base::FilePath& iio_sysfs_dir,
                                   const base::FilePath& device_file)
    : iio_sysfs_dir_(iio_sysfs_dir), device_file_(device_file) {
  bindings_.set_connection_error_handler(base::BindRepeating(
      []() { LOG(INFO) << "SensorDevice connection closed."; }));
}
SensorDeviceImpl::~SensorDeviceImpl() = default;

void SensorDeviceImpl::Bind(mojom::SensorDeviceRequest request) {
  bindings_.AddBinding(this, std::move(request));
}

void SensorDeviceImpl::GetAttribute(const std::string& name,
                                    GetAttributeCallback callback) {
  // Read /sys/bus/iio/devices/iio:deviceX/<name>.
  base::FilePath path = GetAttributePath(iio_sysfs_dir_, name);
  if (path.empty()) {
    LOG(ERROR) << "Invalid name: " << name;
    std::move(callback).Run(mojom::AttributeIOResult::ERROR_IO, {});
    return;
  }
  std::string value;
  if (!base::ReadFileToString(path, &value)) {
    LOG(ERROR) << "Failed to read " << path.value();
    std::move(callback).Run(mojom::AttributeIOResult::ERROR_IO, {});
    return;
  }
  value = base::TrimString(value, "\n", base::TRIM_TRAILING).as_string();
  std::move(callback).Run(mojom::AttributeIOResult::SUCCESS, std::move(value));
}

void SensorDeviceImpl::SetAttribute(const std::string& name,
                                    const std::string& value,
                                    SetAttributeCallback callback) {
  // Write /sys/bus/iio/devices/iio:deviceX/<name>.
  base::FilePath path = GetAttributePath(iio_sysfs_dir_, name);
  if (path.empty()) {
    LOG(ERROR) << "Invalid name: " << name;
    std::move(callback).Run(mojom::AttributeIOResult::ERROR_IO);
    return;
  }
  if (!base::WriteFile(path, value.data(), value.size())) {
    LOG(ERROR) << "Failed to write " << path.value() << ", value = " << value;
    std::move(callback).Run(mojom::AttributeIOResult::ERROR_IO);
    return;
  }
  std::move(callback).Run(mojom::AttributeIOResult::SUCCESS);
}

void SensorDeviceImpl::OpenBuffer(OpenBufferCallback callback) {
  // Open /dev/iio:deviceX.
  base::ScopedFD device_fd(
      HANDLE_EINTR(open(device_file_.value().c_str(), O_RDONLY)));
  if (!device_fd.is_valid()) {
    PLOG(ERROR) << "open failed: " << device_file_.value();
    std::move(callback).Run({});
    return;
  }
  // Create a pipe.
  int pipe_fds[2];
  if (pipe(pipe_fds) < 0) {
    PLOG(ERROR) << "pipe failed";
    std::move(callback).Run({});
    return;
  }
  base::ScopedFD pipe_read_end(pipe_fds[0]), pipe_write_end(pipe_fds[1]);
  // The device file cannot cross the VM boundary. Instead, we return a pipe
  // from this method.
  // Data read from the device file will be forwarded to the pipe.
  device_fd_watcher_ = base::FileDescriptorWatcher::WatchReadable(
      device_fd.get(),
      base::BindRepeating(&SensorDeviceImpl::OnDeviceFdReadReady,
                          base::Unretained(this)));
  device_fd_ = std::move(device_fd);
  pipe_write_end_ = std::move(pipe_write_end);
  // Return the pipe read end to the caller.
  std::move(callback).Run(mojo::WrapPlatformFile(pipe_read_end.release()));
}

void SensorDeviceImpl::OnDeviceFdReadReady() {
  char buf[4096];
  ssize_t read_size = HANDLE_EINTR(read(device_fd_.get(), buf, sizeof(buf)));
  if (read_size < 0) {
    PLOG(ERROR) << "read failed.";
    device_fd_watcher_.reset();
    pipe_write_end_.reset();
    return;
  }
  for (ssize_t written = 0; written < read_size;) {
    ssize_t r = HANDLE_EINTR(
        write(pipe_write_end_.get(), buf + written, read_size - written));
    if (r < 0) {
      PLOG(ERROR) << "write failed.";
      device_fd_watcher_.reset();
      pipe_write_end_.reset();
      return;
    }
    written += r;
  }
}

}  // namespace arc
