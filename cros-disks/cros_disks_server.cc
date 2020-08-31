// Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/cros_disks_server.h"

#include <utility>

#include <base/logging.h>
#include <chromeos/dbus/service_constants.h>

#include "cros-disks/device_event.h"
#include "cros-disks/disk.h"
#include "cros-disks/disk_manager.h"
#include "cros-disks/disk_monitor.h"
#include "cros-disks/error_logger.h"
#include "cros-disks/format_manager.h"
#include "cros-disks/partition_manager.h"
#include "cros-disks/platform.h"
#include "cros-disks/quote.h"
#include "cros-disks/rename_manager.h"

namespace cros_disks {

CrosDisksServer::CrosDisksServer(scoped_refptr<dbus::Bus> bus,
                                 Platform* platform,
                                 DiskMonitor* disk_monitor,
                                 FormatManager* format_manager,
                                 PartitionManager* partition_manager,
                                 RenameManager* rename_manager)
    : org::chromium::CrosDisksAdaptor(this),
      dbus_object_(nullptr, bus, dbus::ObjectPath(kCrosDisksServicePath)),
      platform_(platform),
      disk_monitor_(disk_monitor),
      format_manager_(format_manager),
      partition_manager_(partition_manager),
      rename_manager_(rename_manager) {
  CHECK(platform_) << "Invalid platform object";
  CHECK(disk_monitor_) << "Invalid disk monitor object";
  CHECK(format_manager_) << "Invalid format manager object";
  CHECK(partition_manager_) << "Invalid partition manager object";
  CHECK(rename_manager_) << "Invalid rename manager object";

  format_manager_->set_observer(this);
  rename_manager_->set_observer(this);
}

void CrosDisksServer::RegisterAsync(
    const brillo::dbus_utils::AsyncEventSequencer::CompletionAction& cb) {
  RegisterWithDBusObject(&dbus_object_);
  dbus_object_.RegisterAsync(cb);
}

void CrosDisksServer::RegisterMountManager(MountManager* mount_manager) {
  CHECK(mount_manager) << "Invalid mount manager object";
  mount_managers_.push_back(mount_manager);
}

void CrosDisksServer::Format(const std::string& path,
                             const std::string& filesystem_type,
                             const std::vector<std::string>& options) {
  FormatErrorType error_type = FORMAT_ERROR_NONE;
  Disk disk;
  if (!disk_monitor_->GetDiskByDevicePath(base::FilePath(path), &disk)) {
    error_type = FORMAT_ERROR_INVALID_DEVICE_PATH;
  } else if (disk.is_on_boot_device) {
    error_type = FORMAT_ERROR_DEVICE_NOT_ALLOWED;
  } else {
    error_type = format_manager_->StartFormatting(path, disk.device_file,
                                                  filesystem_type, options);
  }

  if (error_type != FORMAT_ERROR_NONE) {
    LOG(ERROR) << "Could not format device " << quote(path) << " as filesystem "
               << quote(filesystem_type) << ": " << error_type;
    SendFormatCompletedSignal(error_type, path);
  }
}

void CrosDisksServer::SinglePartitionFormat(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<uint32_t>> response,
    const std::string& path) {
  Disk disk;

  if (!disk_monitor_->GetDiskByDevicePath(base::FilePath(path), &disk)) {
    LOG(ERROR) << "Invalid device path: " << quote(path)
               << " error code: " << PARTITION_ERROR_INVALID_DEVICE_PATH;
    response->Return(PARTITION_ERROR_INVALID_DEVICE_PATH);
  } else if (disk.is_on_boot_device || !disk.is_drive || disk.is_read_only) {
    LOG(ERROR) << "Device not allowed: " << quote(path)
               << " error code: " << PARTITION_ERROR_DEVICE_NOT_ALLOWED;
    response->Return(PARTITION_ERROR_DEVICE_NOT_ALLOWED);
  } else {
    partition_manager_->StartSinglePartitionFormat(
        base::FilePath(disk.device_file),
        base::BindOnce(&CrosDisksServer::OnPartitionCompleted,
                       base::Unretained(this), std::move(response)));
  }
}

void CrosDisksServer::Rename(const std::string& path,
                             const std::string& volume_name) {
  RenameErrorType error_type = RENAME_ERROR_NONE;
  Disk disk;
  if (!disk_monitor_->GetDiskByDevicePath(base::FilePath(path), &disk)) {
    error_type = RENAME_ERROR_INVALID_DEVICE_PATH;
  } else if (disk.is_on_boot_device || disk.is_read_only) {
    error_type = RENAME_ERROR_DEVICE_NOT_ALLOWED;
  } else {
    error_type = rename_manager_->StartRenaming(
        path, disk.device_file, volume_name, disk.filesystem_type);
  }

  if (error_type != RENAME_ERROR_NONE) {
    LOG(ERROR) << "Could not rename device " << quote(path) << " as "
               << quote(volume_name) << ": " << error_type;
    SendRenameCompletedSignal(error_type, path);
  }
}

MountManager* CrosDisksServer::FindMounter(
    const std::string& source_path) const {
  for (const auto& manager : mount_managers_) {
    if (manager->CanMount(source_path)) {
      return manager;
    }
  }
  return nullptr;
}

void CrosDisksServer::Mount(const std::string& source,
                            const std::string& filesystem_type,
                            const std::vector<std::string>& options) {
  MountErrorType error_type = MOUNT_ERROR_INVALID_PATH;
  MountSourceType source_type = MOUNT_SOURCE_INVALID;
  std::string mount_path;

  MountManager* mounter = FindMounter(source);
  if (mounter) {
    source_type = mounter->GetMountSourceType();
    error_type = mounter->Mount(source, filesystem_type, options, &mount_path);
  }

  if (error_type != MOUNT_ERROR_NONE) {
    LOG(ERROR) << "Failed to mount " << quote(source) << ": " << error_type;
  }
  SendMountCompletedSignal(error_type, source, source_type, mount_path);
}

uint32_t CrosDisksServer::Unmount(const std::string& path,
                                  const std::vector<std::string>& options) {
  if (path.empty()) {
    LOG(ERROR) << "Cannot unmount an empty path";
    return MOUNT_ERROR_INVALID_ARGUMENT;
  }

  LOG_IF(WARNING, !options.empty())
      << "Ignoring non-empty unmount options " << quote(options);

  MountErrorType error_type = MOUNT_ERROR_INVALID_PATH;
  for (const auto& manager : mount_managers_) {
    error_type = manager->Unmount(path);
    if (error_type != MOUNT_ERROR_PATH_NOT_MOUNTED)
      break;
  }

  LOG_IF(ERROR, error_type != MOUNT_ERROR_NONE)
      << "Cannot unmount " << quote(path) << ": " << error_type;

  return error_type;
}

void CrosDisksServer::UnmountAll() {
  for (const auto& manager : mount_managers_) {
    manager->UnmountAll();
  }
}

std::vector<std::string> CrosDisksServer::EnumerateDevices() {
  std::vector<Disk> disks = disk_monitor_->EnumerateDisks();
  std::vector<std::string> devices;
  devices.reserve(disks.size());
  for (const auto& disk : disks) {
    devices.push_back(disk.native_path);
  }
  return devices;
}

std::vector<CrosDisksServer::DBusMountEntry>
CrosDisksServer::EnumerateMountEntries() {
  std::vector<DBusMountEntry> dbus_mount_entries;
  for (const auto& manager : mount_managers_) {
    for (const auto& mount_entry : manager->GetMountEntries()) {
      dbus_mount_entries.push_back(
          std::make_tuple(static_cast<uint32_t>(mount_entry.error_type),
                          mount_entry.source_path,
                          static_cast<uint32_t>(mount_entry.source_type),
                          mount_entry.mount_path));
    }
  }
  return dbus_mount_entries;
}

bool CrosDisksServer::GetDeviceProperties(
    brillo::ErrorPtr* error,
    const std::string& device_path,
    brillo::VariantDictionary* properties) {
  Disk disk;
  if (!disk_monitor_->GetDiskByDevicePath(base::FilePath(device_path), &disk)) {
    std::string message =
        "Could not get the properties of device " + device_path;
    LOG(ERROR) << message;
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         kCrosDisksServiceError, message);
    return false;
  }

  brillo::VariantDictionary temp_properties;
  temp_properties[kIsAutoMountable] = disk.is_auto_mountable;
  temp_properties[kDeviceIsDrive] = disk.is_drive;
  temp_properties[kDevicePresentationHide] = disk.is_hidden;
  temp_properties[kDeviceIsMounted] = disk.IsMounted();
  temp_properties[kDeviceIsMediaAvailable] = disk.is_media_available;
  temp_properties[kDeviceIsOnBootDevice] = disk.is_on_boot_device;
  temp_properties[kDeviceIsOnRemovableDevice] = disk.is_on_removable_device;
  temp_properties[kDeviceIsVirtual] = disk.is_virtual;
  temp_properties[kStorageDevicePath] = disk.storage_device_path;
  temp_properties[kDeviceFile] = disk.device_file;
  temp_properties[kIdUuid] = disk.uuid;
  temp_properties[kIdLabel] = disk.label;
  temp_properties[kVendorId] = disk.vendor_id;
  temp_properties[kVendorName] = disk.vendor_name;
  temp_properties[kProductId] = disk.product_id;
  temp_properties[kProductName] = disk.product_name;
  temp_properties[kDriveModel] = disk.drive_model;
  temp_properties[kDeviceMediaType] = static_cast<uint32_t>(disk.media_type);
  temp_properties[kDeviceSize] = disk.device_capacity;
  temp_properties[kDeviceIsReadOnly] = disk.is_read_only;
  temp_properties[kFileSystemType] = disk.filesystem_type;
  temp_properties[kDeviceMountPaths] = disk.mount_paths;
  *properties = std::move(temp_properties);
  return true;
}

void CrosDisksServer::OnFormatCompleted(const std::string& device_path,
                                        FormatErrorType error_type) {
  SendFormatCompletedSignal(error_type, device_path);
}

void CrosDisksServer::OnPartitionCompleted(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<uint32_t>> response,
    const base::FilePath& device_path,
    PartitionErrorType error_type) {
  LOG(ERROR) << "Partitioning for device " << quote(device_path)
             << " completed, result code: " << std::to_string(error_type);
  response->Return(error_type);
}

void CrosDisksServer::OnRenameCompleted(const std::string& device_path,
                                        RenameErrorType error_type) {
  SendRenameCompletedSignal(error_type, device_path);
}

void CrosDisksServer::OnScreenIsLocked() {
  // no-op
}

void CrosDisksServer::OnScreenIsUnlocked() {
  // no-op
}

void CrosDisksServer::OnSessionStarted() {
  for (const auto& manager : mount_managers_) {
    manager->StartSession();
  }
}

void CrosDisksServer::OnSessionStopped() {
  for (const auto& manager : mount_managers_) {
    manager->StopSession();
  }
}

void CrosDisksServer::DispatchDeviceEvent(const DeviceEvent& event) {
  switch (event.event_type) {
    case DeviceEvent::kDeviceAdded:
      SendDeviceAddedSignal(event.device_path);
      break;
    case DeviceEvent::kDeviceScanned:
      SendDeviceScannedSignal(event.device_path);
      break;
    case DeviceEvent::kDeviceRemoved:
      SendDeviceRemovedSignal(event.device_path);
      break;
    case DeviceEvent::kDiskAdded:
      SendDiskAddedSignal(event.device_path);
      break;
    case DeviceEvent::kDiskChanged:
      SendDiskChangedSignal(event.device_path);
      break;
    case DeviceEvent::kDiskRemoved:
      SendDiskRemovedSignal(event.device_path);
      break;
    default:
      break;
  }
}

}  // namespace cros_disks
