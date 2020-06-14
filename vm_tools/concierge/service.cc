// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/service.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <linux/capability.h>
#include <net/route.h>
#include <signal.h>
#include <stdint.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/sendfile.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <sys/xattr.h>
#include <unistd.h>

#include <linux/vm_sockets.h>  // Needs to come after sys/socket.h

#include <algorithm>
#include <iterator>
#include <map>
#include <utility>
#include <vector>

#include <base/base64url.h>
#include <base/bind.h>
#include <base/bind_helpers.h>
#include <base/callback.h>
#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/format_macros.h>
#include <base/guid.h>
#include <base/location.h>
#include <base/logging.h>
#include <base/memory/ref_counted.h>
#include <base/memory/ptr_util.h>
#include <base/optional.h>
#include <base/single_thread_task_runner.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/stringprintf.h>
#include <base/strings/string_split.h>
#include <base/synchronization/waitable_event.h>
#include <base/system/sys_info.h>
#include <base/threading/thread_task_runner_handle.h>
#include <base/time/time.h>
#include <base/version.h>
#include <chromeos/dbus/service_constants.h>
#include <crosvm/qcow_utils.h>
#include <dbus/object_proxy.h>
#include <chromeos/patchpanel/client.h>
#include <vm_cicerone/proto_bindings/cicerone_service.pb.h>
#include <vm_concierge/proto_bindings/concierge_service.pb.h>
#include <vm_protos/proto_bindings/vm_guest.pb.h>
#include <chromeos/constants/vm_tools.h>

#include "vm_tools/concierge/arc_vm.h"
#include "vm_tools/concierge/plugin_vm.h"
#include "vm_tools/concierge/plugin_vm_helper.h"
#include "vm_tools/concierge/seneschal_server_proxy.h"
#include "vm_tools/concierge/ssh_keys.h"
#include "vm_tools/concierge/vm_permission_interface.h"
#include "vm_tools/concierge/vmplugin_dispatcher_interface.h"

using std::string;

namespace vm_tools {
namespace concierge {

namespace {

// Path to the runtime directory used by VMs.
constexpr char kRuntimeDir[] = "/run/vm";

// Default path to VM kernel image and rootfs.
constexpr char kVmDefaultPath[] = "/run/imageloader/cros-termina";

// Name of the VM kernel image.
constexpr char kVmKernelName[] = "vm_kernel";

// Name of the VM rootfs image.
constexpr char kVmRootfsName[] = "vm_rootfs.img";

// Name of the VM tools image to be mounted at kToolsMountPath.
constexpr char kVmToolsDiskName[] = "vm_tools.img";

// Filesystem location to mount VM tools image.
constexpr char kToolsMountPath[] = "/opt/google/cros-containers";

// Filesystem type of VM tools image.
constexpr char kToolsFsType[] = "ext4";

// Maximum number of extra disks to be mounted inside the VM.
constexpr int kMaxExtraDisks = 10;

// How long we should wait for a VM to start up.
// While this timeout might be high, it's meant to be a final failure point, not
// the lower bound of how long it takes.  On a loaded system (like extracting
// large compressed files), it could take 10 seconds to boot.
constexpr base::TimeDelta kVmStartupTimeout = base::TimeDelta::FromSeconds(30);

// crosvm directory name.
constexpr char kCrosvmDir[] = "crosvm";

// crosvm log directory name.
constexpr char kCrosvmLogDir[] = "crosvm/log";

// Plugin VM directory name.
constexpr char kPluginVmDir[] = "pvm";

// Cryptohome root base path.
constexpr char kCryptohomeRoot[] = "/home/root";

// Extended attribute indicating that user has picked a disk size and it should
// not be resized.
constexpr char kDiskImageUserChosenSizeXattr[] =
    "user.crostini.user_chosen_size";

// File extension for raw disk types
constexpr char kRawImageExtension[] = ".img";

// File extension for qcow2 disk types
constexpr char kQcowImageExtension[] = ".qcow2";

// File extension for Plugin VMs disk types
constexpr char kPluginVmImageExtension[] = ".pvm";

// File extension for pstore backend file
constexpr char kPstoreExtension[] = ".pstore";

// Valid file extensions for disk images
constexpr const char* kDiskImageExtensions[] = {kRawImageExtension,
                                                kQcowImageExtension, nullptr};

// Valid file extensions for Plugin VM images
constexpr const char* kPluginVmImageExtensions[] = {kPluginVmImageExtension,
                                                    nullptr};

// Default name to use for a container.
constexpr char kDefaultContainerName[] = "penguin";

// Path to process file descriptors.
constexpr char kProcFileDescriptorsPath[] = "/proc/self/fd/";

// Only allow hex digits in the cryptohome id.
constexpr char kValidCryptoHomeCharacters[] = "abcdefABCDEF0123456789";

constexpr uint64_t kMinimumDiskSize = 1ll * 1024 * 1024 * 1024;  // 1 GiB
constexpr uint64_t kDiskSizeMask = ~4095ll;  // Round to disk block size.

constexpr uint64_t kDefaultIoLimit = 1024 * 1024;  // 1 Mib

// How often we should broadcast state of a disk operation (import or export).
constexpr base::TimeDelta kDiskOpReportInterval =
    base::TimeDelta::FromSeconds(15);

// The minimum kernel version of the host which supports untrusted VMs or a
// trusted VM with nested VM support.
constexpr KernelVersionAndMajorRevision kMinKernelVersionForUntrustedVM =
    std::make_pair(4, 14);

// The minimum kernel version of the host which supports virtio-pmem.
constexpr KernelVersionAndMajorRevision kMinKernelVersionForVirtioPmem =
    std::make_pair(4, 4);

// File path that reports the L1TF vulnerability status.
constexpr const char kL1TFFilePath[] =
    "/sys/devices/system/cpu/vulnerabilities/l1tf";

// File path that reports the MDS vulnerability status.
constexpr const char kMDSFilePath[] =
    "/sys/devices/system/cpu/vulnerabilities/mds";

// Passes |method_call| to |handler| and passes the response to
// |response_sender|. If |handler| returns NULL, an empty response is created
// and sent.
void HandleSynchronousDBusMethodCall(
    base::Callback<std::unique_ptr<dbus::Response>(dbus::MethodCall*)> handler,
    dbus::MethodCall* method_call,
    dbus::ExportedObject::ResponseSender response_sender) {
  std::unique_ptr<dbus::Response> response = handler.Run(method_call);
  if (!response)
    response = dbus::Response::FromMethodCall(method_call);
  response_sender.Run(std::move(response));
}

// Posted to a grpc thread to startup a listener service. Puts a copy of
// the pointer to the grpc server in |server_copy| and then signals |event|.
// It will listen on the address specified in |listener_address|.
void RunListenerService(grpc::Service* listener,
                        const std::string& listener_address,
                        base::WaitableEvent* event,
                        std::shared_ptr<grpc::Server>* server_copy) {
  // We are not interested in getting SIGCHLD or SIGTERM on this thread.
  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGCHLD);
  sigaddset(&mask, SIGTERM);
  sigprocmask(SIG_BLOCK, &mask, nullptr);

  // Build the grpc server.
  grpc::ServerBuilder builder;
  builder.AddListeningPort(listener_address, grpc::InsecureServerCredentials());
  builder.RegisterService(listener);

  std::shared_ptr<grpc::Server> server(builder.BuildAndStart().release());

  *server_copy = server;
  event->Signal();

  if (server) {
    server->Wait();
  }
}

// Sets up a gRPC listener service by starting the |grpc_thread| and posting the
// main task to run for the thread. |listener_address| should be the address the
// gRPC server is listening on. A copy of the pointer to the server is put in
// |server_copy|. Returns true if setup & started successfully, false otherwise.
bool SetupListenerService(base::Thread* grpc_thread,
                          grpc::Service* listener_impl,
                          const std::string& listener_address,
                          std::shared_ptr<grpc::Server>* server_copy) {
  // Start the grpc thread.
  if (!grpc_thread->Start()) {
    LOG(ERROR) << "Failed to start grpc thread";
    return false;
  }

  base::WaitableEvent event(base::WaitableEvent::ResetPolicy::AUTOMATIC,
                            base::WaitableEvent::InitialState::NOT_SIGNALED);
  bool ret = grpc_thread->task_runner()->PostTask(
      FROM_HERE, base::Bind(&RunListenerService, listener_impl,
                            listener_address, &event, server_copy));
  if (!ret) {
    LOG(ERROR) << "Failed to post server startup task to grpc thread";
    return false;
  }

  // Wait for the VM grpc server to start.
  event.Wait();

  if (!server_copy) {
    LOG(ERROR) << "grpc server failed to start";
    return false;
  }

  return true;
}

// Converts an IPv4 address to a string. The result will be stored in |str|
// on success.
bool IPv4AddressToString(const uint32_t address, std::string* str) {
  CHECK(str);

  char result[INET_ADDRSTRLEN];
  if (inet_ntop(AF_INET, &address, result, sizeof(result)) != result) {
    return false;
  }
  *str = std::string(result);
  return true;
}

// Get the path to the latest available cros-termina component.
base::FilePath GetLatestVMPath() {
  base::FilePath component_dir(kVmDefaultPath);
  base::FileEnumerator dir_enum(component_dir, false,
                                base::FileEnumerator::DIRECTORIES);

  base::Version latest_version("0");
  base::FilePath latest_path;

  for (base::FilePath path = dir_enum.Next(); !path.empty();
       path = dir_enum.Next()) {
    base::Version version(path.BaseName().value());
    if (!version.IsValid())
      continue;

    if (version > latest_version) {
      latest_version = version;
      latest_path = path;
    }
  }

  return latest_path;
}

// Gets the path to the file given the name, user id, location, and extension.
base::Optional<base::FilePath> GetFilePathFromName(
    const std::string& cryptohome_id,
    const std::string& vm_name,
    StorageLocation storage_location,
    const std::string& extension,
    bool create_parent_dir) {
  if (!base::ContainsOnlyChars(cryptohome_id, kValidCryptoHomeCharacters)) {
    LOG(ERROR) << "Invalid cryptohome_id specified";
    return base::nullopt;
  }
  // Base64 encode the given disk name to ensure it only has valid characters.
  std::string encoded_name;
  base::Base64UrlEncode(vm_name, base::Base64UrlEncodePolicy::INCLUDE_PADDING,
                        &encoded_name);

  base::FilePath storage_dir =
      base::FilePath(kCryptohomeRoot).Append(cryptohome_id);
  switch (storage_location) {
    case STORAGE_CRYPTOHOME_ROOT: {
      storage_dir = storage_dir.Append(kCrosvmDir);
      break;
    }
    case STORAGE_CRYPTOHOME_PLUGINVM: {
      storage_dir = storage_dir.Append(kPluginVmDir);
      break;
    }
    default: {
      LOG(ERROR) << "Unknown storage location type";
      return base::nullopt;
    }
  }
  if (!base::DirectoryExists(storage_dir)) {
    if (!create_parent_dir) {
      return base::nullopt;
    }
    base::File::Error dir_error;

    if (!base::CreateDirectoryAndGetError(storage_dir, &dir_error)) {
      LOG(ERROR) << "Failed to create storage directory in /home/root: "
                 << base::File::ErrorToString(dir_error);
      return base::nullopt;
    }
  }
  return storage_dir.Append(encoded_name).AddExtension(extension);
}

// Gets the path to a VM disk given the name, user id, and location.
bool GetDiskPathFromName(
    const std::string& vm_name,
    const std::string& cryptohome_id,
    StorageLocation storage_location,
    bool create_parent_dir,
    base::FilePath* path_out,
    enum DiskImageType preferred_image_type = DiskImageType::DISK_IMAGE_AUTO) {
  switch (storage_location) {
    case STORAGE_CRYPTOHOME_ROOT: {
      const auto qcow2_path =
          GetFilePathFromName(cryptohome_id, vm_name, storage_location,
                              kQcowImageExtension, create_parent_dir);
      if (!qcow2_path) {
        if (create_parent_dir)
          LOG(ERROR) << "Failed to get qcow2 path";
        return false;
      }
      const auto raw_path =
          GetFilePathFromName(cryptohome_id, vm_name, storage_location,
                              kRawImageExtension, create_parent_dir);
      if (!raw_path) {
        if (create_parent_dir)
          LOG(ERROR) << "Failed to get raw path";
        return false;
      }

      const bool qcow2_exists = base::PathExists(*qcow2_path);
      const bool raw_exists = base::PathExists(*raw_path);

      // This scenario (both <name>.img and <name>.qcow2 exist) should never
      // happen. It is prevented by the later checks in this function.
      // However, in case it does happen somehow (e.g. user manually created
      // files in dev mode), bail out, since we can't tell which one the user
      // wants.
      if (qcow2_exists && raw_exists) {
        LOG(ERROR) << "Both qcow2 and raw variants of " << vm_name
                   << " already exist.";
        return false;
      }

      // Return the path to an existing image of any type, if one exists.
      // If not, generate a path based on the preferred image type.
      if (qcow2_exists) {
        *path_out = *qcow2_path;
      } else if (raw_exists) {
        *path_out = *raw_path;
      } else if (preferred_image_type == DISK_IMAGE_QCOW2) {
        *path_out = *qcow2_path;
      } else if (preferred_image_type == DISK_IMAGE_RAW ||
                 preferred_image_type == DISK_IMAGE_AUTO) {
        *path_out = *raw_path;
      } else {
        LOG(ERROR) << "Unknown image type " << preferred_image_type;
        return false;
      }
      return true;
    }
    case STORAGE_CRYPTOHOME_PLUGINVM: {
      const auto plugin_path =
          GetFilePathFromName(cryptohome_id, vm_name, storage_location,
                              kPluginVmImageExtension, create_parent_dir);
      if (!plugin_path) {
        if (create_parent_dir)
          LOG(ERROR) << "failed to get plugin path";
        return false;
      }
      *path_out = *plugin_path;
      return true;
    }
    default:
      LOG(ERROR) << "Unknown storage location type";
      return false;
  }
}

bool CheckVmExists(const std::string& vm_name,
                   const std::string& cryptohome_id,
                   base::FilePath* out_path = nullptr,
                   StorageLocation* storage_location = nullptr) {
  for (int l = StorageLocation_MIN; l <= StorageLocation_MAX; l++) {
    StorageLocation location = static_cast<StorageLocation>(l);
    base::FilePath disk_path;
    if (GetDiskPathFromName(vm_name, cryptohome_id, location,
                            false, /* create_parent_dir */
                            &disk_path) &&
        base::PathExists(disk_path)) {
      if (out_path) {
        *out_path = disk_path;
      }
      if (storage_location) {
        *storage_location = location;
      }
      return true;
    }
  }

  return false;
}

uint64_t CalculateDesiredDiskSize(uint64_t current_usage) {
  // If no disk size was specified, use 90% of free space.
  // Free space is calculated as if the disk image did not consume any space.
  uint64_t free_space =
      base::SysInfo::AmountOfFreeDiskSpace(base::FilePath("/home"));
  free_space += current_usage;
  uint64_t disk_size = ((free_space * 9) / 10) & kDiskSizeMask;

  return std::max(disk_size, kMinimumDiskSize);
}

// Returns true if the disk size was specified by the user and should not be
// automatically resized.
bool IsDiskUserChosenSize(std::string disk_path) {
  return getxattr(disk_path.c_str(), kDiskImageUserChosenSizeXattr, NULL, 0) >=
         0;
}

// Mark a disk with an xattr indicating its size has been chosen by the user.
bool SetUserChosenSizeAttr(const base::ScopedFD& fd) {
  // The xattr value doesn't matter, only its existence.
  // Store something human-readable for debugging.
  constexpr char val[] = "1";
  return fsetxattr(fd.get(), kDiskImageUserChosenSizeXattr, val, sizeof(val),
                   0) == 0;
}

bool GetPluginDirectory(const base::FilePath& prefix,
                        const string& extension,
                        const string& vm_id,
                        bool create,
                        base::FilePath* path_out) {
  string dirname;
  base::Base64UrlEncode(vm_id, base::Base64UrlEncodePolicy::INCLUDE_PADDING,
                        &dirname);

  base::FilePath path = prefix.Append(dirname).AddExtension(extension);
  if (create && !base::DirectoryExists(path)) {
    base::File::Error dir_error;
    if (!base::CreateDirectoryAndGetError(path, &dir_error)) {
      LOG(ERROR) << "Failed to create plugin directory " << path.value() << ": "
                 << base::File::ErrorToString(dir_error);
      return false;
    }
  }

  *path_out = path;
  return true;
}

bool GetPluginStatefulDirectory(const string& vm_id,
                                const string& cryptohome_id,
                                base::FilePath* path_out) {
  return GetPluginDirectory(base::FilePath(kCryptohomeRoot)
                                .Append(cryptohome_id)
                                .Append(kPluginVmDir),
                            "pvm", vm_id, true /* create */, path_out);
}

bool GetPluginIsoDirectory(const string& vm_id,
                           const string& cryptohome_id,
                           bool create,
                           base::FilePath* path_out) {
  return GetPluginDirectory(base::FilePath(kCryptohomeRoot)
                                .Append(cryptohome_id)
                                .Append(kPluginVmDir),
                            "iso", vm_id, create, path_out);
}

bool GetPluginRuntimeDirectory(const string& vm_id,
                               base::ScopedTempDir* runtime_dir_out) {
  base::FilePath path;
  if (GetPluginDirectory(base::FilePath("/run/pvm"), "", vm_id,
                         true /*create */, &path)) {
    // Take ownership of directory
    CHECK(runtime_dir_out->Set(path));
    return true;
  }

  return false;
}

bool GetPluginRootDirectory(const string& vm_id,
                            base::ScopedTempDir* root_dir_out) {
  base::FilePath path;
  if (!base::CreateTemporaryDirInDir(base::FilePath(kRuntimeDir), "vm.",
                                     &path)) {
    PLOG(ERROR) << "Unable to create root directory for VM";
    return false;
  }

  // Take ownership of directory
  CHECK(root_dir_out->Set(path));
  return true;
}

bool CreatePluginRootHierarchy(const base::FilePath& root_path) {
  base::FilePath etc_dir(root_path.Append("etc"));
  base::File::Error dir_error;
  if (!CreateDirectoryAndGetError(etc_dir, &dir_error)) {
    LOG(ERROR) << "Unable to create /etc in root directory for VM "
               << base::File::ErrorToString(dir_error);
    return false;
  }

  // Note that this will be dangling (or rather point to concierge's timezone
  // instance) until crosvm bind mounts /var/lib/timezone and
  // /usr/share/zoneinfo into plugin jail.
  if (!base::CreateSymbolicLink(base::FilePath("/var/lib/timezone/localtime"),
                                etc_dir.Append("localtime"))) {
    PLOG(ERROR) << "Unable to create /etc/localtime symlink";
    return false;
  }

  return true;
}

bool GetPlugin9PSocketPath(const string& vm_id, base::FilePath* path_out) {
  base::FilePath runtime_dir;
  if (!GetPluginDirectory(base::FilePath("/run/pvm"), "", vm_id,
                          true /* create */, &runtime_dir)) {
    LOG(ERROR) << "Unable to get runtime directory for 9P socket";
    return false;
  }

  *path_out = runtime_dir.Append("9p.sock");
  return true;
}

void FormatDiskImageStatus(const DiskImageOperation* op,
                           DiskImageStatusResponse* status) {
  status->set_status(op->status());
  status->set_command_uuid(op->uuid());
  status->set_failure_reason(op->failure_reason());
  status->set_progress(op->GetProgress());
}

uint64_t GetFileUsage(const base::FilePath& path) {
  struct stat st;
  if (stat(path.value().c_str(), &st) == 0) {
    // Use the st_blocks value to get the space usage (as in 'du') of the file.
    // st_blocks is always in units of 512 bytes, regardless of the underlying
    // filesystem and block device block size.
    return st.st_blocks * 512;
  }
  return 0;
}

// Returns the current kernel version. If there is a failure to retrieve the
// version it returns <INT_MIN, INT_MIN>.
KernelVersionAndMajorRevision GetKernelVersion() {
  struct utsname buf;
  if (uname(&buf))
    return std::make_pair(INT_MIN, INT_MIN);

  // Parse uname result in the form of x.yy.zzz. The parsed data should be in
  // the expected format.
  std::vector<base::StringPiece> versions = base::SplitStringPiece(
      buf.release, ".", base::WhitespaceHandling::TRIM_WHITESPACE,
      base::SplitResult::SPLIT_WANT_ALL);
  DCHECK_EQ(versions.size(), 3);
  DCHECK(!versions[0].empty());
  DCHECK(!versions[1].empty());
  int version;
  bool result = base::StringToInt(versions[0], &version);
  DCHECK(result);
  int major_revision;
  result = base::StringToInt(versions[1], &major_revision);
  DCHECK(result);
  return std::make_pair(version, major_revision);
}

base::FilePath GetVmLogPath(const std::string& owner_id,
                            const std::string& vm_name,
                            bool log_to_cryptohome = false) {
  if (!log_to_cryptohome) {
    return base::FilePath();
  }
  std::string encoded_vm_name;
  base::Base64UrlEncode(vm_name, base::Base64UrlEncodePolicy::OMIT_PADDING,
                        &encoded_vm_name);

  base::FilePath path =
      base::FilePath(kCryptohomeRoot)
          .Append(owner_id)
          .Append(kCrosvmLogDir)
          .Append(base::StringPrintf(
              "%s-%s-%" PRId64 ".log", kCrosvmDir, encoded_vm_name.c_str(),
              base::Time::Now().ToDeltaSinceWindowsEpoch().InMicroseconds()));

  base::FilePath parent_dir = path.DirName();
  if (!base::DirectoryExists(parent_dir)) {
    base::File::Error dir_error;
    if (!base::CreateDirectoryAndGetError(parent_dir, &dir_error)) {
      LOG(ERROR) << "Failed to create crosvm/log directory in /home/root: "
                 << base::File::ErrorToString(dir_error);
      return base::FilePath();
    }
  }
  return path;
}

}  // namespace

bool Service::ListVmDisksInLocation(const string& cryptohome_id,
                                    StorageLocation location,
                                    const string& lookup_name,
                                    ListVmDisksResponse* response) {
  base::FilePath image_dir;
  base::FileEnumerator::FileType file_type = base::FileEnumerator::FILES;
  const char* const* allowed_ext = kDiskImageExtensions;
  switch (location) {
    case STORAGE_CRYPTOHOME_ROOT:
      image_dir = base::FilePath(kCryptohomeRoot)
                      .Append(cryptohome_id)
                      .Append(kCrosvmDir);
      break;

    case STORAGE_CRYPTOHOME_PLUGINVM:
      image_dir = base::FilePath(kCryptohomeRoot)
                      .Append(cryptohome_id)
                      .Append(kPluginVmDir);
      file_type = base::FileEnumerator::DIRECTORIES;
      allowed_ext = kPluginVmImageExtensions;
      break;

    default:
      response->set_success(false);
      response->set_failure_reason("Unsupported storage location for images");
      return false;
  }

  if (!base::DirectoryExists(image_dir)) {
    // No directory means no VMs, return the empty response.
    return true;
  }

  uint64_t total_size = 0;
  base::FileEnumerator dir_enum(image_dir, false, file_type);
  for (base::FilePath path = dir_enum.Next(); !path.empty();
       path = dir_enum.Next()) {
    string extension = path.BaseName().Extension();
    bool allowed = false;
    for (auto p = allowed_ext; *p; p++) {
      if (extension == *p) {
        allowed = true;
        break;
      }
    }
    if (!allowed) {
      continue;
    }

    base::FilePath bare_name = path.BaseName().RemoveExtension();
    if (bare_name.empty()) {
      continue;
    }
    std::string image_name;
    if (!base::Base64UrlDecode(bare_name.value(),
                               base::Base64UrlDecodePolicy::IGNORE_PADDING,
                               &image_name)) {
      continue;
    }
    if (!lookup_name.empty() && lookup_name != image_name) {
      continue;
    }

    uint64_t size = dir_enum.GetInfo().IsDirectory()
                        ? ComputeDirectorySize(path)
                        : GetFileUsage(path);
    total_size += size;

    uint64_t min_size;
    auto iter = FindVm(cryptohome_id, image_name);
    if (iter == vms_.end()) {
      // VM may not be running - in this case, we can't determine min_size, so
      // report 0 for unknown.
      min_size = 0;
    } else {
      min_size = iter->second->GetMinDiskSize();
    }

    enum DiskImageType image_type = DiskImageType::DISK_IMAGE_AUTO;
    if (extension == kRawImageExtension) {
      image_type = DiskImageType::DISK_IMAGE_RAW;
    } else if (extension == kQcowImageExtension) {
      image_type = DiskImageType::DISK_IMAGE_QCOW2;
    } else if (extension == kPluginVmImageExtension) {
      image_type = DiskImageType::DISK_IMAGE_PLUGINVM;
    }

    VmDiskInfo* image = response->add_images();
    image->set_name(std::move(image_name));
    image->set_storage_location(location);
    image->set_size(size);
    image->set_min_size(min_size);
    image->set_image_type(image_type);
    image->set_user_chosen_size(IsDiskUserChosenSize(path.value()));
  }

  response->set_total_size(response->total_size() + total_size);
  return true;
}

std::unique_ptr<Service> Service::Create(base::Closure quit_closure) {
  auto service = base::WrapUnique(new Service(std::move(quit_closure)));

  if (!service->Init()) {
    service.reset();
  }

  return service;
}

Service::Service(base::Closure quit_closure)
    : next_seneschal_server_port_(kFirstSeneschalServerPort),
      quit_closure_(std::move(quit_closure)),
      host_kernel_version_(GetKernelVersion()),
      weak_ptr_factory_(this) {
}

Service::~Service() {
  if (grpc_server_vm_) {
    grpc_server_vm_->Shutdown();
  }
}

void Service::OnSignalReadable() {
  struct signalfd_siginfo siginfo;
  if (read(signal_fd_.get(), &siginfo, sizeof(siginfo)) != sizeof(siginfo)) {
    PLOG(ERROR) << "Failed to read from signalfd";
    return;
  }

  if (siginfo.ssi_signo == SIGCHLD) {
    HandleChildExit();
  } else if (siginfo.ssi_signo == SIGTERM) {
    HandleSigterm();
  } else {
    LOG(ERROR) << "Received unknown signal from signal fd: "
               << strsignal(siginfo.ssi_signo);
  }
}

bool Service::Init() {
  dbus::Bus::Options opts;
  opts.bus_type = dbus::Bus::SYSTEM;
  bus_ = new dbus::Bus(std::move(opts));

  if (!bus_->Connect()) {
    LOG(ERROR) << "Failed to connect to system bus";
    return false;
  }

  exported_object_ =
      bus_->GetExportedObject(dbus::ObjectPath(kVmConciergeServicePath));
  if (!exported_object_) {
    LOG(ERROR) << "Failed to export " << kVmConciergeServicePath << " object";
    return false;
  }

  dbus::ObjectProxy* debugd_proxy = bus_->GetObjectProxy(
      debugd::kDebugdServiceName, dbus::ObjectPath(debugd::kDebugdServicePath));
  if (!debugd_proxy) {
    LOG(ERROR) << "Unable to get dbus proxy for " << debugd::kDebugdServiceName;
    return false;
  }
  untrusted_vm_utils_ = std::make_unique<UntrustedVMUtils>(
      debugd_proxy, host_kernel_version_, kMinKernelVersionForUntrustedVM,
      base::FilePath(kL1TFFilePath), base::FilePath(kMDSFilePath));

  using ServiceMethod =
      std::unique_ptr<dbus::Response> (Service::*)(dbus::MethodCall*);
  const std::map<const char*, ServiceMethod> kServiceMethods = {
      {kStartVmMethod, &Service::StartVm},
      {kStartPluginVmMethod, &Service::StartPluginVm},
      {kStartArcVmMethod, &Service::StartArcVm},
      {kStopVmMethod, &Service::StopVm},
      {kStopAllVmsMethod, &Service::StopAllVms},
      {kSuspendVmMethod, &Service::SuspendVm},
      {kResumeVmMethod, &Service::ResumeVm},
      {kGetVmInfoMethod, &Service::GetVmInfo},
      {kGetVmEnterpriseReportingInfoMethod,
       &Service::GetVmEnterpriseReportingInfo},
      {kAdjustVmMethod, &Service::AdjustVm},
      {kCreateDiskImageMethod, &Service::CreateDiskImage},
      {kDestroyDiskImageMethod, &Service::DestroyDiskImage},
      {kResizeDiskImageMethod, &Service::ResizeDiskImage},
      {kExportDiskImageMethod, &Service::ExportDiskImage},
      {kImportDiskImageMethod, &Service::ImportDiskImage},
      {kDiskImageStatusMethod, &Service::CheckDiskImageStatus},
      {kCancelDiskImageMethod, &Service::CancelDiskImageOperation},
      {kListVmDisksMethod, &Service::ListVmDisks},
      {kGetContainerSshKeysMethod, &Service::GetContainerSshKeys},
      {kSyncVmTimesMethod, &Service::SyncVmTimes},
      {kAttachUsbDeviceMethod, &Service::AttachUsbDevice},
      {kDetachUsbDeviceMethod, &Service::DetachUsbDevice},
      {kListUsbDeviceMethod, &Service::ListUsbDevices},
      {kGetDnsSettingsMethod, &Service::GetDnsSettings},
      {kSetVmCpuRestrictionMethod, &Service::SetVmCpuRestriction},
  };

  for (const auto& iter : kServiceMethods) {
    bool ret = exported_object_->ExportMethodAndBlock(
        kVmConciergeInterface, iter.first,
        base::Bind(&HandleSynchronousDBusMethodCall,
                   base::Bind(iter.second, base::Unretained(this))));
    if (!ret) {
      LOG(ERROR) << "Failed to export method " << iter.first;
      return false;
    }
  }

  if (!bus_->RequestOwnershipAndBlock(kVmConciergeServiceName,
                                      dbus::Bus::REQUIRE_PRIMARY)) {
    LOG(ERROR) << "Failed to take ownership of " << kVmConciergeServiceName;
    return false;
  }

  // Set up the D-Bus client for shill.
  shill_client_ = std::make_unique<ShillClient>(bus_);
  shill_client_->RegisterResolvConfigChangedHandler(base::Bind(
      &Service::OnResolvConfigChanged, weak_ptr_factory_.GetWeakPtr()));
  shill_client_->RegisterDefaultServiceChangedHandler(
      base::Bind(&Service::OnDefaultNetworkServiceChanged,
                 weak_ptr_factory_.GetWeakPtr()));

  // Set up the D-Bus client for powerd and register suspend/resume handlers.
  power_manager_client_ = std::make_unique<PowerManagerClient>(bus_);
  power_manager_client_->RegisterSuspendDelay(
      base::Bind(&Service::HandleSuspendImminent,
                 weak_ptr_factory_.GetWeakPtr()),
      base::Bind(&Service::HandleSuspendDone, weak_ptr_factory_.GetWeakPtr()));

  // Get the D-Bus proxy for communicating with cicerone.
  cicerone_service_proxy_ = bus_->GetObjectProxy(
      vm_tools::cicerone::kVmCiceroneServiceName,
      dbus::ObjectPath(vm_tools::cicerone::kVmCiceroneServicePath));
  if (!cicerone_service_proxy_) {
    LOG(ERROR) << "Unable to get dbus proxy for "
               << vm_tools::cicerone::kVmCiceroneServiceName;
    return false;
  }
  cicerone_service_proxy_->ConnectToSignal(
      vm_tools::cicerone::kVmCiceroneServiceName,
      vm_tools::cicerone::kTremplinStartedSignal,
      base::Bind(&Service::OnTremplinStartedSignal,
                 weak_ptr_factory_.GetWeakPtr()),
      base::Bind(&Service::OnSignalConnected, weak_ptr_factory_.GetWeakPtr()));

  // Get the D-Bus proxy for communicating with seneschal.
  seneschal_service_proxy_ = bus_->GetObjectProxy(
      vm_tools::seneschal::kSeneschalServiceName,
      dbus::ObjectPath(vm_tools::seneschal::kSeneschalServicePath));
  if (!seneschal_service_proxy_) {
    LOG(ERROR) << "Unable to get dbus proxy for "
               << vm_tools::seneschal::kSeneschalServiceName;
    return false;
  }

  // Get the D-Bus proxy for communicating with Plugin VM dispatcher.
  vm_permission_service_proxy_ = vm_permission::GetServiceProxy(bus_);
  if (!vm_permission_service_proxy_) {
    LOG(ERROR) << "Unable to get dbus proxy for VM permission service";
    return false;
  }

  // Get the D-Bus proxy for communicating with Plugin VM dispatcher.
  vmplugin_service_proxy_ = pvm::dispatcher::GetServiceProxy(bus_);
  if (!vmplugin_service_proxy_) {
    LOG(ERROR) << "Unable to get dbus proxy for Plugin VM dispatcher service";
    return false;
  }
  pvm::dispatcher::RegisterVmToolsChangedCallbacks(
      vmplugin_service_proxy_,
      base::Bind(&Service::OnVmToolsStateChangedSignal,
                 weak_ptr_factory_.GetWeakPtr()),
      base::Bind(&Service::OnSignalConnected, weak_ptr_factory_.GetWeakPtr()));

  // Setup & start the gRPC listener services.
  if (!SetupListenerService(
          &grpc_thread_vm_, &startup_listener_,
          base::StringPrintf("vsock:%u:%u", VMADDR_CID_ANY,
                             vm_tools::kDefaultStartupListenerPort),
          &grpc_server_vm_)) {
    LOG(ERROR) << "Failed to setup/startup the VM grpc server";
    return false;
  }

  // Change the umask so that the runtime directory for each VM will get the
  // right permissions.
  umask(002);

  // Set up the signalfd for receiving SIGCHLD and SIGTERM.
  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGCHLD);
  sigaddset(&mask, SIGTERM);

  // Restore process' "dumpable" flag so that /proc will be writable.
  // We need it to properly set up jail for Plugin VM helper process.
  if (prctl(PR_SET_DUMPABLE, 1) < 0) {
    PLOG(ERROR) << "Failed to set PR_SET_DUMPABLE";
    return false;
  }

  signal_fd_.reset(signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC));
  if (!signal_fd_.is_valid()) {
    PLOG(ERROR) << "Failed to create signalfd";
    return false;
  }

  watcher_ = base::FileDescriptorWatcher::WatchReadable(
      signal_fd_.get(),
      base::BindRepeating(&Service::OnSignalReadable, base::Unretained(this)));
  if (!watcher_) {
    LOG(ERROR) << "Failed to watch signalfd";
    return false;
  }

  // Now block signals from the normal signal handling path so that we will get
  // them via the signalfd.
  if (sigprocmask(SIG_BLOCK, &mask, nullptr) < 0) {
    PLOG(ERROR) << "Failed to block signals via sigprocmask";
    return false;
  }

  return true;
}

void Service::HandleChildExit() {
  DCHECK(sequence_checker_.CalledOnValidSequence());
  // We can't just rely on the information in the siginfo structure because
  // more than one child may have exited but only one SIGCHLD will be
  // generated.
  while (true) {
    int status;
    pid_t pid = waitpid(-1, &status, WNOHANG);
    if (pid <= 0) {
      if (pid == -1 && errno != ECHILD) {
        PLOG(ERROR) << "Unable to reap child processes";
      }
      break;
    }

    if (WIFEXITED(status)) {
      if (WEXITSTATUS(status) != 0) {
        LOG(INFO) << "Process " << pid << " exited with status "
                  << WEXITSTATUS(status);
      }
    } else if (WIFSIGNALED(status)) {
      LOG(INFO) << "Process " << pid << " killed by signal " << WTERMSIG(status)
                << (WCOREDUMP(status) ? " (core dumped)" : "");
    } else {
      LOG(WARNING) << "Unknown exit status " << status << " for process "
                   << pid;
    }

    // See if this is a process we launched.
    auto iter = std::find_if(vms_.begin(), vms_.end(), [=](auto& pair) {
      VmInterface::Info info = pair.second->GetInfo();
      return pid == info.pid;
    });

    if (iter != vms_.end()) {
      // Notify that the VM has exited.
      NotifyVmStopped(iter->first, iter->second->GetInfo().cid);

      // Now remove it from the vm list.
      vms_.erase(iter);
    }
  }
}

void Service::HandleSigterm() {
  LOG(INFO) << "Shutting down due to SIGTERM";

  base::ThreadTaskRunnerHandle::Get()->PostTask(FROM_HERE, quit_closure_);
}

template <class StartXXRequest>
std::tuple<bool, StartXXRequest, StartVmResponse> Service::StartVmHelper(
    dbus::MethodCall* method_call,
    dbus::MessageReader* reader,
    dbus::MessageWriter* writer,
    bool allow_zero_cpus) {
  DCHECK(sequence_checker_.CalledOnValidSequence());

  StartXXRequest request;
  StartVmResponse response;
  // We change to a success status later if necessary.
  response.set_status(VM_STATUS_FAILURE);

  if (!reader->PopArrayOfBytesAsProto(&request)) {
    LOG(ERROR) << "Unable to parse StartVmRequest from message";
    response.set_failure_reason("Unable to parse protobuf");
    writer->AppendProtoAsArrayOfBytes(response);
    return {false, request, response};
  }

  // Check the CPU count.
  if ((request.cpus() == 0 && !allow_zero_cpus) ||
      request.cpus() > base::SysInfo::NumberOfProcessors()) {
    LOG(ERROR) << "Invalid number of CPUs: " << request.cpus();
    response.set_failure_reason("Invalid CPU count");
    writer->AppendProtoAsArrayOfBytes(response);
    return {false, request, response};
  }

  // Make sure the VM has a name.
  if (request.name().empty()) {
    LOG(ERROR) << "Ignoring request with empty name";
    response.set_failure_reason("Missing VM name");
    writer->AppendProtoAsArrayOfBytes(response);
    return {false, request, response};
  }

  auto iter = FindVm(request.owner_id(), request.name());
  if (iter != vms_.end()) {
    LOG(INFO) << "VM with requested name is already running";

    VmInterface::Info vm = iter->second->GetInfo();

    VmInfo* vm_info = response.mutable_vm_info();
    vm_info->set_ipv4_address(vm.ipv4_address);
    vm_info->set_pid(vm.pid);
    vm_info->set_cid(vm.cid);
    vm_info->set_seneschal_server_handle(vm.seneschal_server_handle);
    switch (vm.status) {
      case VmInterface::Status::STARTING: {
        response.set_status(VM_STATUS_STARTING);
        break;
      }
      case VmInterface::Status::RUNNING: {
        response.set_status(VM_STATUS_RUNNING);
        break;
      }
      default: {
        response.set_status(VM_STATUS_UNKNOWN);
        break;
      }
    }
    response.set_success(true);

    writer->AppendProtoAsArrayOfBytes(response);
    return {false, request, response};
  }

  return {true, request, response};
}

std::unique_ptr<dbus::Response> Service::StartVm(
    dbus::MethodCall* method_call) {
  LOG(INFO) << "Received StartVm request";

  bool success;
  std::unique_ptr<dbus::Response> dbus_response(
      dbus::Response::FromMethodCall(method_call));
  dbus::MessageReader reader(method_call);
  dbus::MessageWriter writer(dbus_response.get());
  StartVmRequest request;
  StartVmResponse response;
  std::tie(success, request, response) = StartVmHelper<StartVmRequest>(
      method_call, &reader, &writer, true /* allow_zero_cpus */);

  if (!success) {
    return dbus_response;
  }

  // Make sure we have our signal connected if starting a Termina VM.
  if (request.start_termina() && !is_tremplin_started_signal_connected_) {
    LOG(ERROR) << "Can't start Termina VM without TremplinStartedSignal";
    response.set_failure_reason("TremplinStartedSignal not connected");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  if (request.disks_size() > kMaxExtraDisks) {
    LOG(ERROR) << "Rejecting request with " << request.disks_size()
               << " extra disks";

    response.set_failure_reason("Too many extra disks");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  base::FilePath kernel, rootfs, tools_disk;

  // A VM is trusted when this daemon chooses the kernel and rootfs path.
  bool is_trusted_vm = false;
  if (request.start_termina()) {
    base::FilePath component_path = GetLatestVMPath();
    if (component_path.empty()) {
      LOG(ERROR) << "Termina component is not loaded";
      response.set_failure_reason("Termina component is not loaded");
      writer.AppendProtoAsArrayOfBytes(response);
      return dbus_response;
    }

    kernel = component_path.Append(kVmKernelName);
    rootfs = component_path.Append(kVmRootfsName);
    tools_disk = component_path.Append(kVmToolsDiskName);
    is_trusted_vm = true;
  } else if (!request.allow_untrusted()) {
    LOG(ERROR) << "Untrusted VMs aren't allowed";
    response.set_failure_reason("Untrusted VMs aren't allowed");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  } else {
    kernel = base::FilePath(request.vm().kernel());
    rootfs = base::FilePath(request.vm().rootfs());
  }

  if (!base::PathExists(kernel)) {
    LOG(ERROR) << "Missing VM kernel path: " << kernel.value();

    response.set_failure_reason("Kernel path does not exist");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  if (!base::PathExists(rootfs)) {
    LOG(ERROR) << "Missing VM rootfs path: " << rootfs.value();

    response.set_failure_reason("Rootfs path does not exist");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  if (!is_trusted_vm) {
    switch (untrusted_vm_utils_->CheckUntrustedVMMitigationStatus()) {
      case UntrustedVMUtils::MitigationStatus::NOT_VULNERABLE:
        break;

      // If the host kernel version isn't supported or the host doesn't have
      // l1tf and mds mitigations then fail to start an untrusted VM.
      case UntrustedVMUtils::MitigationStatus::VULNERABLE: {
        LOG(ERROR) << "Host vulnerable against untrusted VM";
        response.set_failure_reason("Host vulnerable against untrusted VM");
        writer.AppendProtoAsArrayOfBytes(response);
        return dbus_response;
      }

      // This case is handled immediately after.
      case UntrustedVMUtils::MitigationStatus::VULNERABLE_DUE_TO_SMT_ENABLED:
        break;
    }
  }

  // Nested virtualization is turned on for all host kernels that support
  // untrusted VMs. For security purposes this requires that SMT is disabled for
  // both trusted and untrusted VMs.
  if (host_kernel_version_ >= kMinKernelVersionForUntrustedVM) {
    if (!untrusted_vm_utils_->DisableSMT()) {
      LOG(ERROR) << "Failed to disable SMT";
      response.set_failure_reason("Failed to disable SMT");
      writer.AppendProtoAsArrayOfBytes(response);
      return dbus_response;
    }
  }

  std::vector<TerminaVm::Disk> disks;
  base::ScopedFD storage_fd;
  // Check if an opened storage image was passed over D-BUS.
  if (request.use_fd_for_storage()) {
    if (!reader.PopFileDescriptor(&storage_fd)) {
      LOG(ERROR) << "use_fd_for_storage is set but no fd found";

      response.set_failure_reason("use_fd_for_storage is set but no fd found");
      writer.AppendProtoAsArrayOfBytes(response);
      return dbus_response;
    }
    // Clear close-on-exec as this FD needs to be passed to crosvm.
    int raw_fd = storage_fd.get();
    int flags = fcntl(raw_fd, F_GETFD);
    if (flags == -1) {
      LOG(ERROR) << "Failed to get flags for passed fd";

      response.set_failure_reason("Failed to get flags for passed fd");
      writer.AppendProtoAsArrayOfBytes(response);
      return dbus_response;
    }
    flags &= ~FD_CLOEXEC;
    if (fcntl(raw_fd, F_SETFD, flags) == -1) {
      LOG(ERROR) << "Failed to clear close-on-exec flag for fd";

      response.set_failure_reason("Failed to clear close-on-exec flag for fd");
      writer.AppendProtoAsArrayOfBytes(response);
      return dbus_response;
    }

    base::FilePath fd_path = base::FilePath(kProcFileDescriptorsPath)
                                 .Append(base::NumberToString(raw_fd));
    disks.push_back(TerminaVm::Disk{
        .path = std::move(fd_path),
        .writable = true,
    });
  }

  // Track the next available virtio-blk device name.
  // Assume that the rootfs filesystem was assigned /dev/pmem0 if
  // pmem is used, /dev/vda otherwise.
  // Assume every subsequent image was assigned a letter in alphabetical order
  // starting from 'b'.
  bool use_pmem = host_kernel_version_ >= kMinKernelVersionForVirtioPmem &&
                  USE_PMEM_DEVICE_FOR_ROOTFS;
  string rootfs_device = use_pmem ? "/dev/pmem0" : "/dev/vda";
  unsigned char disk_letter = use_pmem ? 'a' : 'b';

  // In newer components, the /opt/google/cros-containers directory
  // is split into its own disk image(vm_tools.img).  Detect whether it exists
  // to keep compatibility with older components with only vm_rootfs.img.
  string tools_device;
  if (base::PathExists(tools_disk)) {
    disks.push_back(TerminaVm::Disk{
        .path = std::move(tools_disk),
        .writable = false,
    });
    tools_device = base::StringPrintf("/dev/vd%c", disk_letter++);
  }

  if (request.disks().size() == 0) {
    LOG(ERROR) << "Missing required stateful disk";
    response.set_failure_reason("Missing required stateful disk");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  // Assume the stateful device is the first disk in the request.
  string stateful_device = base::StringPrintf("/dev/vd%c", disk_letter);

  auto stateful_path = base::FilePath(request.disks()[0].path());
  int64_t stateful_size = -1;
  if (!base::GetFileSize(stateful_path, &stateful_size)) {
    LOG(ERROR) << "Could not determine stateful disk size";
    response.set_failure_reason(
        "Internal error: unable to determine stateful disk size");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  for (const auto& disk : request.disks()) {
    if (!base::PathExists(base::FilePath(disk.path()))) {
      LOG(ERROR) << "Missing disk path: " << disk.path();
      response.set_failure_reason("One or more disk paths do not exist");
      writer.AppendProtoAsArrayOfBytes(response);
      return dbus_response;
    }

    disks.push_back(TerminaVm::Disk{
        .path = base::FilePath(disk.path()),
        .writable = disk.writable(),
        .sparse = !IsDiskUserChosenSize(disk.path()),
    });
  }

  // Create the runtime directory.
  base::FilePath runtime_dir;
  if (!base::CreateTemporaryDirInDir(base::FilePath(kRuntimeDir), "vm.",
                                     &runtime_dir)) {
    PLOG(ERROR) << "Unable to create runtime directory for VM";

    response.set_failure_reason(
        "Internal error: unable to create runtime directory");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  base::FilePath log_path = GetVmLogPath(request.owner_id(), request.name());

  // Allocate resources for the VM.
  uint32_t vsock_cid = vsock_cid_pool_.Allocate();
  if (vsock_cid == 0) {
    LOG(ERROR) << "Unable to allocate vsock context id";

    response.set_failure_reason("Unable to allocate vsock cid");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  std::unique_ptr<patchpanel::Client> network_client =
      patchpanel::Client::New();
  if (!network_client) {
    LOG(ERROR) << "Unable to open networking service client";

    response.set_failure_reason("Unable to open network service client");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  uint32_t seneschal_server_port = next_seneschal_server_port_++;
  std::unique_ptr<SeneschalServerProxy> server_proxy =
      SeneschalServerProxy::CreateVsockProxy(
          seneschal_service_proxy_, seneschal_server_port, vsock_cid, {}, {});
  if (!server_proxy) {
    LOG(ERROR) << "Unable to start shared directory server";

    response.set_failure_reason("Unable to start shared directory server");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  uint32_t seneschal_server_handle = server_proxy->handle();

  // Associate a WaitableEvent with this VM.  This needs to happen before
  // starting the VM to avoid a race where the VM reports that it's ready
  // before it gets added as a pending VM.
  base::WaitableEvent event(base::WaitableEvent::ResetPolicy::AUTOMATIC,
                            base::WaitableEvent::InitialState::NOT_SIGNALED);
  startup_listener_.AddPendingVm(vsock_cid, &event);

  // Start the VM and build the response.
  VmFeatures features{
      .gpu = request.enable_gpu(),
      .software_tpm = request.software_tpm(),
      .audio_capture = request.enable_audio_capture(),
  };

  // We use _SC_NPROCESSORS_ONLN here rather than
  // base::SysInfo::NumberOfProcessors() so that offline CPUs are not counted.
  // Also, |untrusted_vm_utils_| may disable SMT leading to cores being
  // disabled. Hence, only allocate the lower of (available cores, cpus
  // allocated by the user).
  const int32_t cpus =
      request.cpus() == 0
          ? sysconf(_SC_NPROCESSORS_ONLN)
          : std::min(static_cast<int32_t>(sysconf(_SC_NPROCESSORS_ONLN)),
                     static_cast<int32_t>(request.cpus()));

  auto vm = TerminaVm::Create(
      std::move(kernel), std::move(rootfs), cpus, std::move(disks), vsock_cid,
      std::move(network_client), std::move(server_proxy),
      std::move(runtime_dir), std::move(log_path), std::move(rootfs_device),
      std::move(stateful_device), std::move(stateful_size), features);
  if (!vm) {
    LOG(ERROR) << "Unable to start VM";

    startup_listener_.RemovePendingVm(vsock_cid);
    response.set_failure_reason("Unable to start VM");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  // Wait for the VM to finish starting up and for maitre'd to signal that it's
  // ready.
  if (!event.TimedWait(kVmStartupTimeout)) {
    LOG(ERROR) << "VM failed to start in " << kVmStartupTimeout.InSeconds()
               << " seconds";

    startup_listener_.RemovePendingVm(vsock_cid);
    response.set_failure_reason("VM failed to start in time");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  // maitre'd is ready.  Finish setting up the VM.
  if (!vm->ConfigureNetwork(nameservers_, search_domains_)) {
    LOG(ERROR) << "Failed to configure VM network";

    response.set_failure_reason("Failed to configure VM network");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  // Mount the tools disk if it exists.
  if (!tools_device.empty()) {
    if (!vm->Mount(tools_device, kToolsMountPath, kToolsFsType, MS_RDONLY,
                   "")) {
      LOG(ERROR) << "Failed to mount tools disk";
      response.set_failure_reason("Failed to mount tools disk");
      writer.AppendProtoAsArrayOfBytes(response);
      return dbus_response;
    }
  }

  // Do all the mounts.
  for (const auto& disk : request.disks()) {
    string src = base::StringPrintf("/dev/vd%c", disk_letter++);

    if (!disk.do_mount())
      continue;

    uint64_t flags = disk.flags();
    if (!disk.writable()) {
      flags |= MS_RDONLY;
    }
    if (!vm->Mount(std::move(src), disk.mount_point(), disk.fstype(), flags,
                   disk.data())) {
      LOG(ERROR) << "Failed to mount " << disk.path() << " -> "
                 << disk.mount_point();

      response.set_failure_reason("Failed to mount extra disk");
      writer.AppendProtoAsArrayOfBytes(response);
      return dbus_response;
    }
  }

  // Mount the 9p server.
  if (!vm->Mount9P(seneschal_server_port, "/mnt/shared")) {
    LOG(ERROR) << "Failed to mount " << request.shared_directory();

    response.set_failure_reason("Failed to mount shared directory");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  // Notify cicerone that we have started a VM.
  // We must notify cicerone now before calling StartTermina, but we will only
  // send the VmStartedSignal on success.
  VmId vm_id(request.owner_id(), request.name());
  NotifyCiceroneOfVmStarted(vm_id, vm->cid(), vm->GetInfo().pid, "");

  string failure_reason;
  vm_tools::StartTerminaResponse::MountResult mount_result =
      vm_tools::StartTerminaResponse::UNKNOWN;
  if (request.start_termina() &&
      !StartTermina(vm.get(), &failure_reason, &mount_result)) {
    response.set_failure_reason(std::move(failure_reason));
    response.set_mount_result((StartVmResponse::MountResult)mount_result);
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }
  response.set_mount_result((StartVmResponse::MountResult)mount_result);

  LOG(INFO) << "Started VM with pid " << vm->pid();

  VmInfo* vm_info = response.mutable_vm_info();
  response.set_success(true);
  response.set_status(request.start_termina() ? VM_STATUS_STARTING
                                              : VM_STATUS_RUNNING);
  vm_info->set_ipv4_address(vm->IPv4Address());
  vm_info->set_pid(vm->pid());
  vm_info->set_cid(vsock_cid);
  vm_info->set_seneschal_server_handle(seneschal_server_handle);
  writer.AppendProtoAsArrayOfBytes(response);

  SendVmStartedSignal(vm_id, *vm_info, response.status());

  vms_[vm_id] = std::move(vm);
  return dbus_response;
}

std::unique_ptr<dbus::Response> Service::StartPluginVm(
    dbus::MethodCall* method_call) {
  LOG(INFO) << "Received StartPluginVm request";

  bool success;
  std::unique_ptr<dbus::Response> dbus_response(
      dbus::Response::FromMethodCall(method_call));
  dbus::MessageReader reader(method_call);
  dbus::MessageWriter writer(dbus_response.get());
  StartPluginVmRequest request;
  StartVmResponse response;
  std::tie(success, request, response) =
      StartVmHelper<StartPluginVmRequest>(method_call, &reader, &writer);

  if (!success) {
    return dbus_response;
  }

  // Get the stateful directory.
  base::FilePath stateful_dir;
  if (!GetPluginStatefulDirectory(request.name(), request.owner_id(),
                                  &stateful_dir)) {
    LOG(ERROR) << "Unable to create stateful directory for VM";

    response.set_failure_reason("Unable to create stateful directory");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  // Get the directory for ISO images.
  base::FilePath iso_dir;
  if (!GetPluginIsoDirectory(request.name(), request.owner_id(),
                             true /* create */, &iso_dir)) {
    LOG(ERROR) << "Unable to create directory holding ISOs for VM";

    response.set_failure_reason("Unable to create ISO directory");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  // Create the runtime directory.
  base::ScopedTempDir runtime_dir;
  if (!GetPluginRuntimeDirectory(request.name(), &runtime_dir)) {
    LOG(ERROR) << "Unable to create runtime directory for VM";

    response.set_failure_reason("Unable to create runtime directory");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  // Create the root directory.
  base::ScopedTempDir root_dir;
  if (!GetPluginRootDirectory(request.name(), &root_dir)) {
    LOG(ERROR) << "Unable to create runtime directory for VM";

    response.set_failure_reason("Unable to create runtime directory");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  if (!CreatePluginRootHierarchy(root_dir.GetPath())) {
    response.set_failure_reason("Unable to create plugin root hierarchy");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  if (!PluginVm::WriteResolvConf(root_dir.GetPath().Append("etc"), nameservers_,
                                 search_domains_)) {
    LOG(ERROR) << "Unable to seed resolv.conf for the Plugin VM";

    response.set_failure_reason("Unable to seed resolv.conf");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  // Generate the token used by cicerone to identify the VM and write it to
  // a VM specific directory that gets mounted into the VM.
  std::string vm_token = base::GenerateGUID();
  if (base::WriteFile(runtime_dir.GetPath().Append("cicerone.token"),
                      vm_token.c_str(),
                      vm_token.length()) != vm_token.length()) {
    PLOG(ERROR) << "Failure writing out cicerone token to file";

    response.set_failure_reason("Unable to set cicerone token");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  base::FilePath p9_socket_path;
  if (!GetPlugin9PSocketPath(request.name(), &p9_socket_path)) {
    response.set_failure_reason("Internal error: unable to get 9P directory");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  base::ScopedFD p9_socket =
      PluginVm::CreateUnixSocket(p9_socket_path, SOCK_STREAM);
  if (!p9_socket.is_valid()) {
    LOG(ERROR) << "Failed creating 9P socket for file sharing";

    response.set_failure_reason("Internal error: unable to create 9P socket");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  std::unique_ptr<patchpanel::Client> network_client =
      patchpanel::Client::New();
  if (!network_client) {
    LOG(ERROR) << "Unable to open networking service client";

    response.set_failure_reason("Unable to open network service client");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  std::unique_ptr<SeneschalServerProxy> seneschal_server_proxy =
      SeneschalServerProxy::CreateFdProxy(seneschal_service_proxy_, p9_socket);
  if (!seneschal_server_proxy) {
    LOG(ERROR) << "Unable to start shared directory server";

    response.set_failure_reason("Unable to start shared directory server");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  // Build the plugin params.
  std::vector<string> params(
      std::make_move_iterator(request.mutable_params()->begin()),
      std::make_move_iterator(request.mutable_params()->end()));

  // Now start the VM.
  VmId vm_id(request.owner_id(), request.name());
  std::unique_ptr<PluginVm> vm = PluginVm::Create(
      vm_id, request.cpus(), std::move(params), std::move(stateful_dir),
      std::move(iso_dir), root_dir.Take(), runtime_dir.Take(),
      std::move(network_client), request.subnet_index(),
      request.net_options().enable_vnet_hdr(),
      std::move(seneschal_server_proxy), vm_permission_service_proxy_,
      vmplugin_service_proxy_);
  if (!vm) {
    LOG(ERROR) << "Unable to start VM";
    response.set_failure_reason("Unable to start VM");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  VmInterface::Info info = vm->GetInfo();

  VmInfo* vm_info = response.mutable_vm_info();
  vm_info->set_ipv4_address(info.ipv4_address);
  vm_info->set_pid(info.pid);
  vm_info->set_cid(info.cid);
  vm_info->set_seneschal_server_handle(info.seneschal_server_handle);
  switch (info.status) {
    case VmInterface::Status::STARTING: {
      response.set_status(VM_STATUS_STARTING);
      break;
    }
    case VmInterface::Status::RUNNING: {
      response.set_status(VM_STATUS_RUNNING);
      break;
    }
    default: {
      response.set_status(VM_STATUS_UNKNOWN);
      break;
    }
  }
  response.set_success(true);
  writer.AppendProtoAsArrayOfBytes(response);

  NotifyCiceroneOfVmStarted(vm_id, 0 /* cid */, info.pid, std::move(vm_token));
  SendVmStartedSignal(vm_id, *vm_info, response.status());

  vms_[vm_id] = std::move(vm);
  return dbus_response;
}

std::unique_ptr<dbus::Response> Service::StartArcVm(
    dbus::MethodCall* method_call) {
  LOG(INFO) << "Received StartArcVm request";
  bool success;
  std::unique_ptr<dbus::Response> dbus_response(
      dbus::Response::FromMethodCall(method_call));
  dbus::MessageReader reader(method_call);
  dbus::MessageWriter writer(dbus_response.get());
  StartArcVmRequest request;
  StartVmResponse response;
  std::tie(success, request, response) =
      StartVmHelper<StartArcVmRequest>(method_call, &reader, &writer);

  if (!success) {
    return dbus_response;
  }

  if (request.disks_size() > kMaxExtraDisks) {
    LOG(ERROR) << "Rejecting request with " << request.disks_size()
               << " extra disks";

    response.set_failure_reason("Too many extra disks");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  const base::FilePath kernel(request.vm().kernel());
  const base::FilePath rootfs(request.vm().rootfs());
  const base::FilePath fstab(request.fstab());

  if (!base::PathExists(kernel)) {
    LOG(ERROR) << "Missing VM kernel path: " << kernel.value();

    response.set_failure_reason("Kernel path does not exist");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  if (!base::PathExists(rootfs)) {
    LOG(ERROR) << "Missing VM rootfs path: " << rootfs.value();

    response.set_failure_reason("Rootfs path does not exist");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  if (!base::PathExists(fstab)) {
    LOG(ERROR) << "Missing VM fstab path: " << fstab.value();

    response.set_failure_reason("Fstab path does not exist");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  std::vector<ArcVm::Disk> disks;
  for (const auto& disk : request.disks()) {
    if (!base::PathExists(base::FilePath(disk.path()))) {
      LOG(ERROR) << "Missing disk path: " << disk.path();
      response.set_failure_reason("One or more disk paths do not exist");
      writer.AppendProtoAsArrayOfBytes(response);
      return dbus_response;
    }
    disks.push_back(ArcVm::Disk{
        .path = base::FilePath(disk.path()),
        .writable = disk.writable(),
    });
  }

  // Create the runtime directory.
  base::FilePath runtime_dir;
  if (!base::CreateTemporaryDirInDir(base::FilePath(kRuntimeDir), "vm.",
                                     &runtime_dir)) {
    PLOG(ERROR) << "Unable to create runtime directory for VM";

    response.set_failure_reason(
        "Internal error: unable to create runtime directory");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  // Allocate resources for the VM.
  uint32_t vsock_cid = vsock_cid_pool_.Allocate();
  if (vsock_cid == 0) {
    LOG(ERROR) << "Unable to allocate vsock context id";

    response.set_failure_reason("Unable to allocate vsock cid");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  std::unique_ptr<patchpanel::Client> network_client =
      patchpanel::Client::New();
  if (!network_client) {
    LOG(ERROR) << "Unable to open networking service client";

    response.set_failure_reason("Unable to open network service client");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  // Map the chronos user (1000) and the chronos-access group (1001) to the
  // AID_EXTERNAL_STORAGE user and group (1077).
  uint32_t seneschal_server_port = next_seneschal_server_port_++;
  std::unique_ptr<SeneschalServerProxy> server_proxy =
      SeneschalServerProxy::CreateVsockProxy(seneschal_service_proxy_,
                                             seneschal_server_port, vsock_cid,
                                             {{1000, 1077}}, {{1001, 1077}});
  if (!server_proxy) {
    LOG(ERROR) << "Unable to start shared directory server";

    response.set_failure_reason("Unable to start shared directory server");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  uint32_t seneschal_server_handle = server_proxy->handle();

  // Build the plugin params.
  std::vector<string> params(
      std::make_move_iterator(request.mutable_params()->begin()),
      std::make_move_iterator(request.mutable_params()->end()));
  params.emplace_back(base::StringPrintf("androidboot.seneschal_server_port=%d",
                                         seneschal_server_port));

  // Start the VM and build the response.
  ArcVmFeatures features;
  features.rootfs_writable = request.rootfs_writable();

  const auto pstore_path = GetFilePathFromName(
      request.owner_id(), request.name(), STORAGE_CRYPTOHOME_ROOT,
      kPstoreExtension, true /* create_parent_dir */);
  if (!pstore_path) {
    LOG(ERROR) << "Failed to get pstore path";

    response.set_failure_reason("Failed to get pstore path");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }
  const uint32_t pstore_size = 1024 * 1024;

  base::FilePath data_dir = base::FilePath("/home/root")
                                .Append(request.owner_id())
                                .Append("android-data")
                                .Append("data");
  if (!base::PathExists(data_dir)) {
    LOG(WARNING) << "Android data directory does not exist";

    response.set_failure_reason("Android data directory does not exist");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  auto vm = ArcVm::Create(
      std::move(kernel), std::move(rootfs), std::move(fstab), request.cpus(),
      std::move(*pstore_path), pstore_size, std::move(disks), vsock_cid,
      std::move(data_dir), std::move(network_client), std::move(server_proxy),
      std::move(runtime_dir), features, std::move(params));
  if (!vm) {
    LOG(ERROR) << "Unable to start VM";

    response.set_failure_reason("Unable to start VM");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  // ARCVM is ready.
  LOG(INFO) << "Started VM with pid " << vm->pid();

  VmInfo* vm_info = response.mutable_vm_info();
  response.set_success(true);
  response.set_status(VM_STATUS_RUNNING);
  vm_info->set_ipv4_address(vm->IPv4Address());
  vm_info->set_pid(vm->pid());
  vm_info->set_cid(vsock_cid);
  vm_info->set_seneschal_server_handle(seneschal_server_handle);
  writer.AppendProtoAsArrayOfBytes(response);

  VmId vm_id(request.owner_id(), request.name());
  SendVmStartedSignal(vm_id, *vm_info, response.status());

  vms_[vm_id] = std::move(vm);
  return dbus_response;
}

std::unique_ptr<dbus::Response> Service::StopVm(dbus::MethodCall* method_call) {
  DCHECK(sequence_checker_.CalledOnValidSequence());
  LOG(INFO) << "Received StopVm request";

  std::unique_ptr<dbus::Response> dbus_response(
      dbus::Response::FromMethodCall(method_call));

  dbus::MessageReader reader(method_call);
  dbus::MessageWriter writer(dbus_response.get());

  StopVmRequest request;
  StopVmResponse response;

  if (!reader.PopArrayOfBytesAsProto(&request)) {
    LOG(ERROR) << "Unable to parse StopVmRequest from message";

    response.set_failure_reason("Unable to parse protobuf");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  auto iter = FindVm(request.owner_id(), request.name());
  if (iter == vms_.end()) {
    LOG(ERROR) << "Requested VM does not exist";
    // This is not an error to Chrome
    response.set_success(true);
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  // Notify that we are about to stop a VM.
  NotifyVmStopping(iter->first, iter->second->GetInfo().cid);

  if (!iter->second->Shutdown()) {
    LOG(ERROR) << "Unable to shut down VM";

    response.set_failure_reason("Unable to shut down VM");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  // Notify that we have stopped a VM.
  NotifyVmStopped(iter->first, iter->second->GetInfo().cid);

  vms_.erase(iter);
  response.set_success(true);
  writer.AppendProtoAsArrayOfBytes(response);

  return dbus_response;
}

std::unique_ptr<dbus::Response> Service::StopAllVms(
    dbus::MethodCall* method_call) {
  DCHECK(sequence_checker_.CalledOnValidSequence());
  LOG(INFO) << "Received StopAllVms request";

  // Spawn a thread for each VM to shut it down.
  for (auto& iter : vms_) {
    // Copy out cid from the VM object, as we will need it after the VM has been
    // destructed.
    auto cid = iter.second->GetInfo().cid;

    // Notify that we are about to stop a VM.
    NotifyVmStopping(iter.first, cid);

    // Resetting the unique_ptr will call the destructor for that VM,
    // which will try stopping it normally (and then forcibly) it if
    // it hasn't stopped yet.
    iter.second.reset();

    // Notify that we have stopped a VM.
    NotifyVmStopped(iter.first, cid);
  }

  vms_.clear();

  return nullptr;
}

std::unique_ptr<dbus::Response> Service::SuspendVm(
    dbus::MethodCall* method_call) {
  DCHECK(sequence_checker_.CalledOnValidSequence());
  LOG(INFO) << "Received SuspendVm request";

  std::unique_ptr<dbus::Response> dbus_response(
      dbus::Response::FromMethodCall(method_call));

  dbus::MessageReader reader(method_call);
  dbus::MessageWriter writer(dbus_response.get());

  SuspendVmRequest request;
  SuspendVmResponse response;

  if (!reader.PopArrayOfBytesAsProto(&request)) {
    LOG(ERROR) << "Unable to parse SuspendVmRequest from message";

    response.set_failure_reason("Unable to parse protobuf");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  auto iter = FindVm(request.owner_id(), request.name());
  if (iter == vms_.end()) {
    LOG(ERROR) << "Requested VM does not exist";
    // This is not an error to Chrome
    response.set_success(true);
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  auto& vm = iter->second;
  if (!vm->UsesExternalSuspendSignals()) {
    LOG(ERROR) << "Received D-Bus suspend request for " << iter->first
               << " but it does not use external suspend signals.";

    response.set_failure_reason(
        "VM does not support external suspend signals.");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  vm->Suspend();

  response.set_success(true);
  writer.AppendProtoAsArrayOfBytes(response);

  return dbus_response;
}

std::unique_ptr<dbus::Response> Service::ResumeVm(
    dbus::MethodCall* method_call) {
  DCHECK(sequence_checker_.CalledOnValidSequence());
  LOG(INFO) << "Received ResumeVm request";

  std::unique_ptr<dbus::Response> dbus_response(
      dbus::Response::FromMethodCall(method_call));

  dbus::MessageReader reader(method_call);
  dbus::MessageWriter writer(dbus_response.get());

  ResumeVmRequest request;
  ResumeVmResponse response;

  if (!reader.PopArrayOfBytesAsProto(&request)) {
    LOG(ERROR) << "Unable to parse ResumeVmRequest from message";

    response.set_failure_reason("Unable to parse protobuf");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  auto iter = FindVm(request.owner_id(), request.name());
  if (iter == vms_.end()) {
    LOG(ERROR) << "Requested VM does not exist";
    // This is not an error to Chrome
    response.set_success(true);
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  auto& vm = iter->second;
  if (!vm->UsesExternalSuspendSignals()) {
    LOG(ERROR) << "Received D-Bus resume request for " << iter->first
               << " but it does not use external suspend signals.";

    response.set_failure_reason(
        "VM does not support external suspend signals.");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  vm->Resume();

  string failure_reason;
  if (vm->SetTime(&failure_reason)) {
    LOG(INFO) << "Successfully set VM clock in " << iter->first << ".";
  } else {
    LOG(ERROR) << "Failed to set VM clock in " << iter->first << ": "
               << failure_reason;
  }

  vm->SetResolvConfig(nameservers_, search_domains_);

  response.set_success(true);
  writer.AppendProtoAsArrayOfBytes(response);

  return dbus_response;
}

std::unique_ptr<dbus::Response> Service::GetVmInfo(
    dbus::MethodCall* method_call) {
  DCHECK(sequence_checker_.CalledOnValidSequence());
  LOG(INFO) << "Received GetVmInfo request";

  std::unique_ptr<dbus::Response> dbus_response(
      dbus::Response::FromMethodCall(method_call));

  dbus::MessageReader reader(method_call);
  dbus::MessageWriter writer(dbus_response.get());

  GetVmInfoRequest request;
  GetVmInfoResponse response;

  if (!reader.PopArrayOfBytesAsProto(&request)) {
    LOG(ERROR) << "Unable to parse GetVmInfoRequest from message";

    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  auto iter = FindVm(request.owner_id(), request.name());
  if (iter == vms_.end()) {
    LOG(ERROR) << "Requested VM does not exist";

    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  VmInterface::Info vm = iter->second->GetInfo();

  VmInfo* vm_info = response.mutable_vm_info();
  vm_info->set_ipv4_address(vm.ipv4_address);
  vm_info->set_pid(vm.pid);
  vm_info->set_cid(vm.cid);
  vm_info->set_seneschal_server_handle(vm.seneschal_server_handle);

  response.set_success(true);
  writer.AppendProtoAsArrayOfBytes(response);

  return dbus_response;
}

std::unique_ptr<dbus::Response> Service::GetVmEnterpriseReportingInfo(
    dbus::MethodCall* method_call) {
  DCHECK(sequence_checker_.CalledOnValidSequence());
  LOG(INFO) << "Received GetVmEnterpriseReportingInfo request";

  std::unique_ptr<dbus::Response> dbus_response(
      dbus::Response::FromMethodCall(method_call));

  dbus::MessageReader reader(method_call);
  dbus::MessageWriter writer(dbus_response.get());

  GetVmEnterpriseReportingInfoRequest request;
  GetVmEnterpriseReportingInfoResponse response;

  response.set_success(false);

  if (!reader.PopArrayOfBytesAsProto(&request)) {
    const std::string error_message =
        "Unable to parse GetVmEnterpriseReportingInfo from message";
    LOG(ERROR) << error_message;
    response.set_failure_reason(error_message);
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  auto iter = FindVm(request.owner_id(), request.vm_name());
  if (iter == vms_.end()) {
    const std::string error_message = "Requested VM does not exist";
    LOG(ERROR) << error_message;
    response.set_failure_reason(error_message);
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  // failure_reason and success will be set by GetVmEnterpriseReportingInfo.
  if (!iter->second->GetVmEnterpriseReportingInfo(&response)) {
    LOG(ERROR) << "Failed to get VM enterprise reporting info";
  }
  writer.AppendProtoAsArrayOfBytes(response);
  return dbus_response;
}

std::unique_ptr<dbus::Response> Service::AdjustVm(
    dbus::MethodCall* method_call) {
  DCHECK(sequence_checker_.CalledOnValidSequence());
  LOG(INFO) << "Received AdjustVm request";

  std::unique_ptr<dbus::Response> dbus_response(
      dbus::Response::FromMethodCall(method_call));

  dbus::MessageReader reader(method_call);
  dbus::MessageWriter writer(dbus_response.get());

  AdjustVmRequest request;
  AdjustVmResponse response;

  response.set_success(false);

  if (!reader.PopArrayOfBytesAsProto(&request)) {
    const std::string error_message =
        "Unable to parse AdjustVmRequest from message";
    LOG(ERROR) << error_message;
    response.set_failure_reason(error_message);
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  StorageLocation location;
  if (!CheckVmExists(request.name(), request.owner_id(), nullptr, &location)) {
    response.set_failure_reason("Requested VM does not exist");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  std::vector<string> params(
      std::make_move_iterator(request.mutable_params()->begin()),
      std::make_move_iterator(request.mutable_params()->end()));

  string failure_reason;
  bool success = false;
  if (request.operation() == "pvm.shared-profile") {
    if (location != STORAGE_CRYPTOHOME_PLUGINVM) {
      failure_reason = "Operation is not supported for the VM";
    } else {
      success = pvm::helper::ToggleSharedProfile(
          vmplugin_service_proxy_, VmId(request.owner_id(), request.name()),
          std::move(params), &failure_reason);
    }
  } else {
    failure_reason = "Unrecognized operation";
  }

  response.set_success(success);
  response.set_failure_reason(failure_reason);
  writer.AppendProtoAsArrayOfBytes(response);
  return dbus_response;
}

std::unique_ptr<dbus::Response> Service::SyncVmTimes(
    dbus::MethodCall* method_call) {
  DCHECK(sequence_checker_.CalledOnValidSequence());
  LOG(INFO) << "Received SyncVmTimes request";

  std::unique_ptr<dbus::Response> dbus_response(
      dbus::Response::FromMethodCall(method_call));

  dbus::MessageWriter writer(dbus_response.get());

  SyncVmTimesResponse response;
  int failures = 0;
  int requests = 0;
  for (auto& vm_entry : vms_) {
    requests++;
    string failure_reason;
    if (!vm_entry.second->SetTime(&failure_reason)) {
      failures++;
      response.add_failure_reason(std::move(failure_reason));
    }
  }
  response.set_requests(requests);
  response.set_failures(failures);

  writer.AppendProtoAsArrayOfBytes(response);

  return dbus_response;
}

bool Service::StartTermina(
    TerminaVm* vm,
    string* failure_reason,
    vm_tools::StartTerminaResponse::MountResult* result) {
  DCHECK(sequence_checker_.CalledOnValidSequence());
  DCHECK(result);
  LOG(INFO) << "Starting Termina-specific services";

  std::string dst_addr;
  IPv4AddressToString(vm->ContainerSubnet(), &dst_addr);
  size_t prefix_length = vm->ContainerPrefixLength();

  std::string container_subnet_cidr =
      base::StringPrintf("%s/%zu", dst_addr.c_str(), prefix_length);

  string error;
  vm_tools::StartTerminaResponse response;
  if (!vm->StartTermina(std::move(container_subnet_cidr), &error, &response)) {
    failure_reason->assign(error);
    return false;
  }

  if (response.mount_result() ==
      vm_tools::StartTerminaResponse::PARTIAL_DATA_LOSS) {
    LOG(ERROR) << "Possible data loss from filesystem corruption detected";
  }

  *result = response.mount_result();

  return true;
}

std::unique_ptr<dbus::Response> Service::CreateDiskImage(
    dbus::MethodCall* method_call) {
  DCHECK(sequence_checker_.CalledOnValidSequence());
  LOG(INFO) << "Received CreateDiskImage request";

  std::unique_ptr<dbus::Response> dbus_response(
      dbus::Response::FromMethodCall(method_call));

  dbus::MessageReader reader(method_call);
  dbus::MessageWriter writer(dbus_response.get());

  CreateDiskImageRequest request;
  CreateDiskImageResponse response;

  if (!reader.PopArrayOfBytesAsProto(&request)) {
    LOG(ERROR) << "Unable to parse CreateDiskImageRequest from message";
    response.set_status(DISK_STATUS_FAILED);
    response.set_failure_reason("Unable to parse CreateImageDiskRequest");

    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  base::FilePath disk_path;
  StorageLocation disk_location;
  if (CheckVmExists(request.disk_path(), request.cryptohome_id(), &disk_path,
                    &disk_location)) {
    if (disk_location != request.storage_location()) {
      response.set_status(DISK_STATUS_FAILED);
      response.set_failure_reason(
          "VM/disk with same name already exists in another storage location");
      writer.AppendProtoAsArrayOfBytes(response);
      return dbus_response;
    }

    if (disk_location == STORAGE_CRYPTOHOME_PLUGINVM) {
      // We do not support extending Plugin VM images.
      response.set_status(DISK_STATUS_FAILED);
      response.set_failure_reason("Plugin VM with such name already exists");
      writer.AppendProtoAsArrayOfBytes(response);
      return dbus_response;
    }

    struct stat st;
    if (stat(disk_path.value().c_str(), &st) < 0) {
      PLOG(ERROR) << "stat() of existing VM image failed for "
                  << disk_path.value();
      response.set_status(DISK_STATUS_FAILED);
      response.set_failure_reason(
          "internal error: image exists but stat() failed");
      writer.AppendProtoAsArrayOfBytes(response);
      return dbus_response;
    }

    uint64_t current_size = st.st_size;
    uint64_t current_usage = st.st_blocks * 512ull;
    LOG(INFO) << "Found existing disk at " << disk_path.value()
              << " with current size " << current_size << " and usage "
              << current_usage;

    // Automatically extend existing disk images if disk_size was not specified.
    if (request.disk_size() == 0) {
      // If the user.crostini.user_chosen_size xattr exists, don't resize the
      // disk. (The value stored in the xattr is ignored; only its existence
      // matters.)
      if (IsDiskUserChosenSize(disk_path.value())) {
        LOG(INFO) << "Disk image has " << kDiskImageUserChosenSizeXattr
                  << " xattr - keeping existing size " << current_size;
      } else {
        uint64_t disk_size = CalculateDesiredDiskSize(current_usage);
        if (disk_size > current_size) {
          LOG(INFO) << "Expanding disk image from " << current_size << " to "
                    << disk_size;
          if (expand_disk_image(disk_path.value().c_str(), disk_size) != 0) {
            // If expanding the disk failed, continue with a warning.
            // Currently, raw images can be resized, and qcow2 images cannot.
            LOG(WARNING) << "Failed to expand disk image " << disk_path.value();
          }
        } else {
          LOG(INFO) << "Current size " << current_size
                    << " is already at least requested size " << disk_size
                    << " - not expanding";
        }
      }
    }

    response.set_status(DISK_STATUS_EXISTS);
    response.set_disk_path(disk_path.value());
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  if (!GetDiskPathFromName(request.disk_path(), request.cryptohome_id(),
                           request.storage_location(),
                           true, /* create_parent_dir */
                           &disk_path, request.image_type())) {
    response.set_status(DISK_STATUS_FAILED);
    response.set_failure_reason("Failed to create vm image");
    writer.AppendProtoAsArrayOfBytes(response);

    return dbus_response;
  }

  if (request.storage_location() == STORAGE_CRYPTOHOME_PLUGINVM) {
    // Get the FD to fill with disk image data.
    base::ScopedFD in_fd;
    if (!reader.PopFileDescriptor(&in_fd)) {
      LOG(ERROR) << "CreateDiskImage: no fd found";
      response.set_failure_reason("no source fd found");
      writer.AppendProtoAsArrayOfBytes(response);
      return dbus_response;
    }

    // Get the name of directory for ISO images. Do not create it - it will be
    // created by the PluginVmCreateOperation code.
    base::FilePath iso_dir;
    if (!GetPluginIsoDirectory(request.disk_path(), request.cryptohome_id(),
                               false /* create */, &iso_dir)) {
      LOG(ERROR) << "Unable to determine directory for ISOs";

      response.set_failure_reason("Unable to determine ISO directory");
      writer.AppendProtoAsArrayOfBytes(response);
      return dbus_response;
    }

    std::vector<string> params(
        std::make_move_iterator(request.mutable_params()->begin()),
        std::make_move_iterator(request.mutable_params()->end()));

    auto op = PluginVmCreateOperation::Create(
        std::move(in_fd), iso_dir, request.source_size(),
        VmId(request.cryptohome_id(), request.disk_path()), std::move(params));

    response.set_disk_path(disk_path.value());
    response.set_status(op->status());
    response.set_command_uuid(op->uuid());
    response.set_failure_reason(op->failure_reason());

    if (op->status() == DISK_STATUS_IN_PROGRESS) {
      std::string uuid = op->uuid();
      disk_image_ops_.emplace_back(DiskOpInfo(std::move(op)));
      base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE,
          base::Bind(&Service::RunDiskImageOperation,
                     weak_ptr_factory_.GetWeakPtr(), std::move(uuid)));
    }

    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  uint64_t disk_size =
      request.disk_size() ? request.disk_size() : CalculateDesiredDiskSize(0);

  if (request.image_type() == DISK_IMAGE_RAW ||
      request.image_type() == DISK_IMAGE_AUTO) {
    LOG(INFO) << "Creating raw disk at: " << disk_path.value() << " size "
              << disk_size;
    base::ScopedFD fd(
        open(disk_path.value().c_str(), O_CREAT | O_NONBLOCK | O_WRONLY, 0600));
    if (!fd.is_valid()) {
      PLOG(ERROR) << "Failed to create raw disk";
      response.set_status(DISK_STATUS_FAILED);
      response.set_failure_reason("Failed to create raw disk file");
      writer.AppendProtoAsArrayOfBytes(response);

      return dbus_response;
    }

    if (request.disk_size() != 0) {
      LOG(INFO)
          << "Disk size specified in request; creating user-chosen-size image";
      if (!SetUserChosenSizeAttr(fd)) {
        PLOG(ERROR) << "Failed to set user_chosen_size xattr";
        unlink(disk_path.value().c_str());
        response.set_status(DISK_STATUS_FAILED);
        response.set_failure_reason("Failed to set user_chosen_size xattr");
        writer.AppendProtoAsArrayOfBytes(response);

        return dbus_response;
      }

      LOG(INFO) << "Preallocating user-chosen-size raw disk image";
      if (fallocate(fd.get(), 0, 0, disk_size) != 0) {
        PLOG(ERROR) << "Failed to allocate raw disk";
        unlink(disk_path.value().c_str());
        response.set_status(DISK_STATUS_FAILED);
        response.set_failure_reason("Failed to allocate raw disk file");
        writer.AppendProtoAsArrayOfBytes(response);

        return dbus_response;
      }

      LOG(INFO) << "Disk image preallocated";
      response.set_status(DISK_STATUS_CREATED);
      response.set_disk_path(disk_path.value());
      writer.AppendProtoAsArrayOfBytes(response);

      return dbus_response;
    }

    LOG(INFO) << "Creating sparse raw disk image";
    int ret = ftruncate(fd.get(), disk_size);
    if (ret != 0) {
      PLOG(ERROR) << "Failed to truncate raw disk";
      unlink(disk_path.value().c_str());
      response.set_status(DISK_STATUS_FAILED);
      response.set_failure_reason("Failed to truncate raw disk file");
      writer.AppendProtoAsArrayOfBytes(response);

      return dbus_response;
    }

    response.set_status(DISK_STATUS_CREATED);
    response.set_disk_path(disk_path.value());
    writer.AppendProtoAsArrayOfBytes(response);

    return dbus_response;
  }

  LOG(INFO) << "Creating qcow2 disk at: " << disk_path.value() << " size "
            << disk_size;
  int ret = create_qcow_with_size(disk_path.value().c_str(), disk_size);
  if (ret != 0) {
    LOG(ERROR) << "Failed to create qcow2 disk image: " << strerror(ret);
    response.set_status(DISK_STATUS_FAILED);
    response.set_failure_reason("Failed to create qcow2 disk image");
    writer.AppendProtoAsArrayOfBytes(response);

    return dbus_response;
  }

  response.set_disk_path(disk_path.value());
  response.set_status(DISK_STATUS_CREATED);
  writer.AppendProtoAsArrayOfBytes(response);

  return dbus_response;
}

std::unique_ptr<dbus::Response> Service::DestroyDiskImage(
    dbus::MethodCall* method_call) {
  DCHECK(sequence_checker_.CalledOnValidSequence());
  LOG(INFO) << "Received DestroyDiskImage request";

  std::unique_ptr<dbus::Response> dbus_response(
      dbus::Response::FromMethodCall(method_call));

  dbus::MessageReader reader(method_call);
  dbus::MessageWriter writer(dbus_response.get());

  DestroyDiskImageRequest request;
  DestroyDiskImageResponse response;

  if (!reader.PopArrayOfBytesAsProto(&request)) {
    LOG(ERROR) << "Unable to parse DestroyDiskImageRequest from message";
    response.set_status(DISK_STATUS_FAILED);
    response.set_failure_reason("Unable to parse DestroyDiskRequest");

    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  // Stop the associated VM if it is still running.
  auto iter = FindVm(request.cryptohome_id(), request.disk_path());
  if (iter != vms_.end()) {
    LOG(INFO) << "Shutting down VM";

    // Notify that we are about to stop a VM.
    NotifyVmStopping(iter->first, iter->second->GetInfo().cid);
    if (!iter->second->Shutdown()) {
      LOG(ERROR) << "Unable to shut down VM";

      response.set_status(DISK_STATUS_FAILED);
      response.set_failure_reason("Unable to shut down VM");
      writer.AppendProtoAsArrayOfBytes(response);
      return dbus_response;
    }

    // Notify that we have stopped a VM.
    NotifyVmStopped(iter->first, iter->second->GetInfo().cid);
    vms_.erase(iter);
  }

  base::FilePath disk_path;
  StorageLocation location;
  if (!CheckVmExists(request.disk_path(), request.cryptohome_id(), &disk_path,
                     &location)) {
    response.set_status(DISK_STATUS_DOES_NOT_EXIST);
    response.set_failure_reason("No such image");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  if (!EraseGuestSshKeys(request.cryptohome_id(), request.disk_path())) {
    // Don't return a failure here, just log an error because this is only a
    // side effect and not what the real request is about.
    LOG(ERROR) << "Failed removing guest SSH keys for VM "
               << request.disk_path();
  }

  if (location == STORAGE_CRYPTOHOME_PLUGINVM) {
    // Plugin VMs need to be unregistered before we can delete them.
    VmId vm_id(request.cryptohome_id(), request.disk_path());
    bool registered;
    if (!pvm::dispatcher::IsVmRegistered(vmplugin_service_proxy_, vm_id,
                                         &registered)) {
      response.set_status(DISK_STATUS_FAILED);
      response.set_failure_reason(
          "failed to check Plugin VM registration status");
      writer.AppendProtoAsArrayOfBytes(response);

      return dbus_response;
    }

    if (registered &&
        !pvm::dispatcher::UnregisterVm(vmplugin_service_proxy_, vm_id)) {
      response.set_status(DISK_STATUS_FAILED);
      response.set_failure_reason("failed to unregister Plugin VM");
      writer.AppendProtoAsArrayOfBytes(response);

      return dbus_response;
    }

    base::FilePath iso_dir;
    if (GetPluginIsoDirectory(vm_id.name(), vm_id.owner_id(),
                              false /* create */, &iso_dir) &&
        base::PathExists(iso_dir) &&
        !base::DeleteFile(iso_dir, true /* recursive */)) {
      LOG(ERROR) << "Unable to remove ISO directory for " << vm_id.name();

      response.set_status(DISK_STATUS_FAILED);
      response.set_failure_reason("Unable to remove ISO directory");
      writer.AppendProtoAsArrayOfBytes(response);

      return dbus_response;
    }
  }

  if (!base::DeleteFile(
          disk_path, location == STORAGE_CRYPTOHOME_PLUGINVM /* recursive */)) {
    response.set_status(DISK_STATUS_FAILED);
    response.set_failure_reason("Disk removal failed");
    writer.AppendProtoAsArrayOfBytes(response);

    return dbus_response;
  }

  response.set_status(DISK_STATUS_DESTROYED);
  writer.AppendProtoAsArrayOfBytes(response);

  return dbus_response;
}

std::unique_ptr<dbus::Response> Service::ResizeDiskImage(
    dbus::MethodCall* method_call) {
  DCHECK(sequence_checker_.CalledOnValidSequence());
  LOG(INFO) << "Received ResizeDiskImage request";

  std::unique_ptr<dbus::Response> dbus_response(
      dbus::Response::FromMethodCall(method_call));

  dbus::MessageReader reader(method_call);
  dbus::MessageWriter writer(dbus_response.get());

  ResizeDiskImageRequest request;
  ResizeDiskImageResponse response;

  if (!reader.PopArrayOfBytesAsProto(&request)) {
    LOG(ERROR) << "Unable to parse ResizeDiskImageRequest from message";
    response.set_status(DISK_STATUS_FAILED);
    response.set_failure_reason("Unable to parse ResizeDiskImageRequest");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  base::FilePath disk_path;
  StorageLocation location;
  if (!CheckVmExists(request.vm_name(), request.cryptohome_id(), &disk_path,
                     &location)) {
    response.set_status(DISK_STATUS_DOES_NOT_EXIST);
    response.set_failure_reason("Resize image doesn't exist");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  auto size = request.disk_size() & kDiskSizeMask;
  if (size != request.disk_size()) {
    LOG(INFO) << "Rounded requested disk size from " << request.disk_size()
              << " to " << size;
  }

  auto op = VmResizeOperation::Create(
      VmId(request.cryptohome_id(), request.vm_name()), location, disk_path,
      size, base::Bind(&Service::ResizeDisk, weak_ptr_factory_.GetWeakPtr()),
      base::Bind(&Service::ProcessResize, weak_ptr_factory_.GetWeakPtr()));

  response.set_status(op->status());
  response.set_command_uuid(op->uuid());
  response.set_failure_reason(op->failure_reason());

  if (op->status() == DISK_STATUS_IN_PROGRESS) {
    std::string uuid = op->uuid();
    disk_image_ops_.emplace_back(DiskOpInfo(std::move(op)));
    base::ThreadTaskRunnerHandle::Get()->PostTask(
        FROM_HERE, base::Bind(&Service::RunDiskImageOperation,
                              weak_ptr_factory_.GetWeakPtr(), std::move(uuid)));
  } else if (op->status() == DISK_STATUS_RESIZED) {
    DiskImageStatus status = DISK_STATUS_RESIZED;
    std::string failure_reason;
    FinishResize(request.cryptohome_id(), request.vm_name(), location, &status,
                 &failure_reason);
    if (status != DISK_STATUS_RESIZED) {
      response.set_status(status);
      response.set_failure_reason(failure_reason);
    }
  }

  writer.AppendProtoAsArrayOfBytes(response);
  return dbus_response;
}

void Service::ResizeDisk(const std::string& owner_id,
                         const std::string& vm_name,
                         StorageLocation location,
                         uint64_t new_size,
                         DiskImageStatus* status,
                         std::string* failure_reason) {
  auto iter = FindVm(owner_id, vm_name);
  if (iter == vms_.end()) {
    LOG(ERROR) << "Unable to find VM " << vm_name;
    *failure_reason = "No such image";
    *status = DISK_STATUS_DOES_NOT_EXIST;
    return;
  }

  *status = iter->second->ResizeDisk(new_size, failure_reason);
}

void Service::ProcessResize(const std::string& owner_id,
                            const std::string& vm_name,
                            StorageLocation location,
                            uint64_t target_size,
                            DiskImageStatus* status,
                            std::string* failure_reason) {
  auto iter = FindVm(owner_id, vm_name);
  if (iter == vms_.end()) {
    LOG(ERROR) << "Unable to find VM " << vm_name;
    *failure_reason = "No such image";
    *status = DISK_STATUS_DOES_NOT_EXIST;
    return;
  }

  *status = iter->second->GetDiskResizeStatus(failure_reason);

  if (*status == DISK_STATUS_RESIZED) {
    FinishResize(owner_id, vm_name, location, status, failure_reason);
  }
}

void Service::FinishResize(const std::string& owner_id,
                           const std::string& vm_name,
                           StorageLocation location,
                           DiskImageStatus* status,
                           std::string* failure_reason) {
  base::FilePath disk_path;
  if (!GetDiskPathFromName(vm_name, owner_id, location,
                           false, /* create_parent_dir */
                           &disk_path)) {
    LOG(ERROR) << "Failed to get disk path after resize";
    *failure_reason = "Failed to get disk path after resize";
    *status = DISK_STATUS_FAILED;
    return;
  }

  base::ScopedFD fd(
      open(disk_path.value().c_str(), O_CREAT | O_NONBLOCK | O_WRONLY, 0600));
  if (!fd.is_valid()) {
    PLOG(ERROR) << "Failed to open disk image";
    *failure_reason = "Failed to open disk image";
    *status = DISK_STATUS_FAILED;
    return;
  }

  // This disk now has a user-chosen size by virtue of being resized.
  if (!SetUserChosenSizeAttr(fd)) {
    LOG(ERROR) << "Failed to set user-chosen size xattr";
    *failure_reason = "Failed to set user-chosen size xattr";
    *status = DISK_STATUS_FAILED;
    return;
  }
}

std::unique_ptr<dbus::Response> Service::ExportDiskImage(
    dbus::MethodCall* method_call) {
  DCHECK(sequence_checker_.CalledOnValidSequence());
  LOG(INFO) << "Received ExportDiskImage request";

  std::unique_ptr<dbus::Response> dbus_response(
      dbus::Response::FromMethodCall(method_call));

  dbus::MessageReader reader(method_call);
  dbus::MessageWriter writer(dbus_response.get());

  ExportDiskImageResponse response;
  response.set_status(DISK_STATUS_FAILED);

  ExportDiskImageRequest request;
  if (!reader.PopArrayOfBytesAsProto(&request)) {
    LOG(ERROR) << "Unable to parse ExportDiskImageRequest from message";
    response.set_failure_reason("Unable to parse ExportDiskRequest");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  base::FilePath disk_path;
  StorageLocation location;
  if (!CheckVmExists(request.disk_path(), request.cryptohome_id(), &disk_path,
                     &location)) {
    response.set_status(DISK_STATUS_DOES_NOT_EXIST);
    response.set_failure_reason("Export image doesn't exist");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  // Get the FD to fill with disk image data.
  base::ScopedFD storage_fd;
  if (!reader.PopFileDescriptor(&storage_fd)) {
    LOG(ERROR) << "export: no fd found";
    response.set_failure_reason("export: no fd found");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  base::ScopedFD digest_fd;
  if (request.generate_sha256_digest() &&
      !reader.PopFileDescriptor(&digest_fd)) {
    LOG(ERROR) << "export: no digest fd found";
    response.set_failure_reason("export: no digest fd found");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  ArchiveFormat fmt;
  switch (location) {
    case STORAGE_CRYPTOHOME_ROOT:
      fmt = ArchiveFormat::TAR_GZ;
      break;
    case STORAGE_CRYPTOHOME_PLUGINVM:
      fmt = ArchiveFormat::ZIP;
      break;
    default:
      LOG(ERROR) << "Unsupported location for source image";
      response.set_failure_reason("Unsupported location for image");
      writer.AppendProtoAsArrayOfBytes(response);
      return dbus_response;
  }

  auto op = VmExportOperation::Create(
      VmId(request.cryptohome_id(), request.disk_path()), disk_path,
      std::move(storage_fd), std::move(digest_fd), fmt);

  response.set_status(op->status());
  response.set_command_uuid(op->uuid());
  response.set_failure_reason(op->failure_reason());

  if (op->status() == DISK_STATUS_IN_PROGRESS) {
    std::string uuid = op->uuid();
    disk_image_ops_.emplace_back(DiskOpInfo(std::move(op)));
    base::ThreadTaskRunnerHandle::Get()->PostTask(
        FROM_HERE, base::Bind(&Service::RunDiskImageOperation,
                              weak_ptr_factory_.GetWeakPtr(), std::move(uuid)));
  }

  writer.AppendProtoAsArrayOfBytes(response);
  return dbus_response;
}

std::unique_ptr<dbus::Response> Service::ImportDiskImage(
    dbus::MethodCall* method_call) {
  DCHECK(sequence_checker_.CalledOnValidSequence());
  LOG(INFO) << "Received ImportDiskImage request";

  std::unique_ptr<dbus::Response> dbus_response(
      dbus::Response::FromMethodCall(method_call));

  dbus::MessageReader reader(method_call);
  dbus::MessageWriter writer(dbus_response.get());

  ImportDiskImageResponse response;
  response.set_status(DISK_STATUS_FAILED);

  ImportDiskImageRequest request;
  if (!reader.PopArrayOfBytesAsProto(&request)) {
    LOG(ERROR) << "Unable to parse ImportDiskImageRequest from message";
    response.set_failure_reason("Unable to parse ImportDiskRequest");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  if (CheckVmExists(request.disk_path(), request.cryptohome_id())) {
    response.set_status(DISK_STATUS_EXISTS);
    response.set_failure_reason("VM/disk with such name already exists");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  if (request.storage_location() != STORAGE_CRYPTOHOME_PLUGINVM) {
    LOG(ERROR)
        << "Locations other than STORAGE_CRYPTOHOME_PLUGINVM are not supported";
    response.set_failure_reason("Unsupported location for image");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  base::FilePath disk_path;
  if (!GetDiskPathFromName(request.disk_path(), request.cryptohome_id(),
                           request.storage_location(),
                           true, /* create_parent_dir */
                           &disk_path)) {
    response.set_failure_reason("Failed to set up vm image name");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  // Get the FD to fill with disk image data.
  base::ScopedFD in_fd;
  if (!reader.PopFileDescriptor(&in_fd)) {
    LOG(ERROR) << "import: no fd found";
    response.set_failure_reason("import: no fd found");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  auto op = PluginVmImportOperation::Create(
      std::move(in_fd), disk_path, request.source_size(),
      VmId(request.cryptohome_id(), request.disk_path()),
      vmplugin_service_proxy_);

  response.set_status(op->status());
  response.set_command_uuid(op->uuid());
  response.set_failure_reason(op->failure_reason());

  if (op->status() == DISK_STATUS_IN_PROGRESS) {
    std::string uuid = op->uuid();
    disk_image_ops_.emplace_back(DiskOpInfo(std::move(op)));
    base::ThreadTaskRunnerHandle::Get()->PostTask(
        FROM_HERE, base::Bind(&Service::RunDiskImageOperation,
                              weak_ptr_factory_.GetWeakPtr(), std::move(uuid)));
  }

  writer.AppendProtoAsArrayOfBytes(response);
  return dbus_response;
}

void Service::RunDiskImageOperation(std::string uuid) {
  auto iter =
      std::find_if(disk_image_ops_.begin(), disk_image_ops_.end(),
                   [&uuid](auto& info) { return info.op->uuid() == uuid; });

  if (iter == disk_image_ops_.end()) {
    LOG(ERROR) << "RunDiskImageOperation called with unknown uuid";
    return;
  }

  if (iter->canceled) {
    // Operation was cancelled. Now that our posted task is running we can
    // remove it from the list and not reschedule ourselves.
    disk_image_ops_.erase(iter);
    return;
  }

  auto op = iter->op.get();
  op->Run(kDefaultIoLimit);
  if (base::TimeTicks::Now() - iter->last_report_time > kDiskOpReportInterval ||
      op->status() != DISK_STATUS_IN_PROGRESS) {
    LOG(INFO) << "Disk Image Operation: UUID=" << uuid
              << " progress: " << op->GetProgress()
              << " status: " << op->status();

    // Send the D-Bus signal out updating progress of the operation.
    DiskImageStatusResponse status;
    FormatDiskImageStatus(op, &status);
    dbus::Signal signal(kVmConciergeInterface, kDiskImageProgressSignal);
    dbus::MessageWriter(&signal).AppendProtoAsArrayOfBytes(status);
    exported_object_->SendSignal(&signal);

    // Note the time we sent out the notification.
    iter->last_report_time = base::TimeTicks::Now();
  }

  if (op->status() == DISK_STATUS_IN_PROGRESS) {
    // Reschedule ourselves so we can execute next chunk of work.
    base::ThreadTaskRunnerHandle::Get()->PostTask(
        FROM_HERE, base::Bind(&Service::RunDiskImageOperation,
                              weak_ptr_factory_.GetWeakPtr(), std::move(uuid)));
  }
}

std::unique_ptr<dbus::Response> Service::CheckDiskImageStatus(
    dbus::MethodCall* method_call) {
  DCHECK(sequence_checker_.CalledOnValidSequence());
  LOG(INFO) << "Received DiskImageStatus request";

  std::unique_ptr<dbus::Response> dbus_response(
      dbus::Response::FromMethodCall(method_call));

  dbus::MessageReader reader(method_call);
  dbus::MessageWriter writer(dbus_response.get());

  DiskImageStatusResponse response;
  response.set_status(DISK_STATUS_FAILED);

  DiskImageStatusRequest request;
  if (!reader.PopArrayOfBytesAsProto(&request)) {
    LOG(ERROR) << "Unable to parse DiskImageStatusRequest from message";
    response.set_failure_reason("Unable to parse DiskImageStatusRequest");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  // Locate the pending command in the list.
  auto iter = std::find_if(disk_image_ops_.begin(), disk_image_ops_.end(),
                           [&request](auto& info) {
                             return info.op->uuid() == request.command_uuid();
                           });

  if (iter == disk_image_ops_.end() || iter->canceled) {
    LOG(ERROR) << "Unknown command uuid in DiskImageStatusRequest";
    response.set_failure_reason("Unknown command uuid");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  auto op = iter->op.get();
  FormatDiskImageStatus(op, &response);
  writer.AppendProtoAsArrayOfBytes(response);

  // Erase operation form the list if it is no longer in progress.
  if (op->status() != DISK_STATUS_IN_PROGRESS) {
    disk_image_ops_.erase(iter);
  }

  return dbus_response;
}

std::unique_ptr<dbus::Response> Service::CancelDiskImageOperation(
    dbus::MethodCall* method_call) {
  DCHECK(sequence_checker_.CalledOnValidSequence());
  LOG(INFO) << "Received CancelDiskImage request";

  std::unique_ptr<dbus::Response> dbus_response(
      dbus::Response::FromMethodCall(method_call));

  dbus::MessageReader reader(method_call);
  dbus::MessageWriter writer(dbus_response.get());

  CancelDiskImageResponse response;
  response.set_success(false);

  CancelDiskImageRequest request;
  if (!reader.PopArrayOfBytesAsProto(&request)) {
    LOG(ERROR) << "Unable to parse CancelDiskImageRequest from message";
    response.set_failure_reason("Unable to parse CancelDiskImageRequest");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  // Locate the pending command in the list.
  auto iter = std::find_if(disk_image_ops_.begin(), disk_image_ops_.end(),
                           [&request](auto& info) {
                             return info.op->uuid() == request.command_uuid();
                           });

  if (iter == disk_image_ops_.end()) {
    LOG(ERROR) << "Unknown command uuid in CancelDiskImageRequest";
    response.set_failure_reason("Unknown command uuid");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  auto op = iter->op.get();
  if (op->status() != DISK_STATUS_IN_PROGRESS) {
    response.set_failure_reason("Command is no longer in progress");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  // Mark the operation as canceled. We can't erase it from the list right
  // away as there is a task posted for it. The task will erase this operation
  // when it gets to run.
  iter->canceled = true;

  response.set_success(true);
  writer.AppendProtoAsArrayOfBytes(response);
  return dbus_response;
}

std::unique_ptr<dbus::Response> Service::ListVmDisks(
    dbus::MethodCall* method_call) {
  DCHECK(sequence_checker_.CalledOnValidSequence());
  std::unique_ptr<dbus::Response> dbus_response(
      dbus::Response::FromMethodCall(method_call));

  dbus::MessageReader reader(method_call);
  dbus::MessageWriter writer(dbus_response.get());

  ListVmDisksRequest request;
  ListVmDisksResponse response;
  if (!reader.PopArrayOfBytesAsProto(&request)) {
    LOG(ERROR) << "Unable to parse ListVmDisksRequest from message";
    response.set_success(false);
    response.set_failure_reason("Unable to parse ListVmDisksRequest");

    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  response.set_success(true);
  response.set_total_size(0);

  for (int location = StorageLocation_MIN; location <= StorageLocation_MAX;
       location++) {
    if (request.all_locations() || location == request.storage_location()) {
      if (!ListVmDisksInLocation(request.cryptohome_id(),
                                 static_cast<StorageLocation>(location),
                                 request.vm_name(), &response)) {
        break;
      }
    }
  }

  writer.AppendProtoAsArrayOfBytes(response);
  return dbus_response;
}

std::unique_ptr<dbus::Response> Service::GetContainerSshKeys(
    dbus::MethodCall* method_call) {
  DCHECK(sequence_checker_.CalledOnValidSequence());
  LOG(INFO) << "Received GetContainerSshKeys request";
  std::unique_ptr<dbus::Response> dbus_response(
      dbus::Response::FromMethodCall(method_call));

  dbus::MessageReader reader(method_call);
  dbus::MessageWriter writer(dbus_response.get());

  ContainerSshKeysRequest request;
  ContainerSshKeysResponse response;
  if (!reader.PopArrayOfBytesAsProto(&request)) {
    LOG(ERROR) << "Unable to parse ContainerSshKeysRequest from message";
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  if (request.cryptohome_id().empty()) {
    LOG(ERROR) << "Cryptohome ID is not set in ContainerSshKeysRequest";
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  auto iter = FindVm(request.cryptohome_id(), request.vm_name());
  if (iter == vms_.end()) {
    LOG(ERROR) << "Requested VM does not exist:" << request.vm_name();
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  std::string container_name = request.container_name().empty()
                                   ? kDefaultContainerName
                                   : request.container_name();
  response.set_container_public_key(GetGuestSshPublicKey(
      request.cryptohome_id(), request.vm_name(), container_name));
  response.set_container_private_key(GetGuestSshPrivateKey(
      request.cryptohome_id(), request.vm_name(), container_name));
  response.set_host_public_key(GetHostSshPublicKey(request.cryptohome_id()));
  response.set_host_private_key(GetHostSshPrivateKey(request.cryptohome_id()));
  response.set_hostname(base::StringPrintf(
      "%s.%s.linux.test", container_name.c_str(), request.vm_name().c_str()));
  writer.AppendProtoAsArrayOfBytes(response);
  return dbus_response;
}

std::unique_ptr<dbus::Response> Service::AttachUsbDevice(
    dbus::MethodCall* method_call) {
  DCHECK(sequence_checker_.CalledOnValidSequence());
  LOG(INFO) << "Received AttachUsbDevice request";

  std::unique_ptr<dbus::Response> dbus_response(
      dbus::Response::FromMethodCall(method_call));

  dbus::MessageReader reader(method_call);
  dbus::MessageWriter writer(dbus_response.get());

  AttachUsbDeviceRequest request;
  AttachUsbDeviceResponse response;
  base::ScopedFD fd;

  response.set_success(false);

  if (!reader.PopArrayOfBytesAsProto(&request)) {
    LOG(ERROR) << "Unable to parse AttachUsbDeviceRequest from message";
    response.set_reason("Unable to parse protobuf");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  if (!reader.PopFileDescriptor(&fd)) {
    LOG(ERROR) << "Unable to parse file descriptor from dbus message";
    response.set_reason("Unable to parse file descriptor");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  auto iter = FindVm(request.owner_id(), request.vm_name());
  if (iter == vms_.end()) {
    LOG(ERROR) << "Requested VM " << request.vm_name() << " does not exist";
    response.set_reason("Requested VM does not exist");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  if (request.bus_number() > 0xFF) {
    LOG(ERROR) << "Bus number out of valid range " << request.bus_number();
    response.set_reason("Invalid bus number");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  if (request.port_number() > 0xFF) {
    LOG(ERROR) << "Port number out of valid range " << request.port_number();
    response.set_reason("Invalid port number");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  if (request.vendor_id() > 0xFFFF) {
    LOG(ERROR) << "Vendor ID out of valid range " << request.vendor_id();
    response.set_reason("Invalid vendor ID");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  if (request.product_id() > 0xFFFF) {
    LOG(ERROR) << "Product ID out of valid range " << request.product_id();
    response.set_reason("Invalid product ID");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  UsbControlResponse usb_response;
  if (!iter->second->AttachUsbDevice(
          request.bus_number(), request.port_number(), request.vendor_id(),
          request.product_id(), fd.get(), &usb_response)) {
    LOG(ERROR) << "Failed to attach USB device: " << usb_response.reason;
    response.set_reason(std::move(usb_response.reason));
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }
  response.set_success(true);
  response.set_guest_port(usb_response.port);
  writer.AppendProtoAsArrayOfBytes(response);
  return dbus_response;
}

std::unique_ptr<dbus::Response> Service::DetachUsbDevice(
    dbus::MethodCall* method_call) {
  DCHECK(sequence_checker_.CalledOnValidSequence());
  LOG(INFO) << "Received DetachUsbDevice request";

  std::unique_ptr<dbus::Response> dbus_response(
      dbus::Response::FromMethodCall(method_call));

  dbus::MessageReader reader(method_call);
  dbus::MessageWriter writer(dbus_response.get());

  DetachUsbDeviceRequest request;
  DetachUsbDeviceResponse response;

  response.set_success(false);

  if (!reader.PopArrayOfBytesAsProto(&request)) {
    LOG(ERROR) << "Unable to parse DetachUsbDeviceRequest from message";
    response.set_reason("Unable to parse protobuf");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  auto iter = FindVm(request.owner_id(), request.vm_name());
  if (iter == vms_.end()) {
    LOG(ERROR) << "Requested VM does not exist";
    response.set_reason("Requested VM does not exist");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  if (request.guest_port() > 0xFF) {
    LOG(ERROR) << "Guest port number out of valid range "
               << request.guest_port();
    response.set_reason("Invalid guest port number");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  UsbControlResponse usb_response;
  if (!iter->second->DetachUsbDevice(request.guest_port(), &usb_response)) {
    LOG(ERROR) << "Failed to detach USB device";
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }
  response.set_success(true);
  writer.AppendProtoAsArrayOfBytes(response);
  return dbus_response;
}

std::unique_ptr<dbus::Response> Service::ListUsbDevices(
    dbus::MethodCall* method_call) {
  DCHECK(sequence_checker_.CalledOnValidSequence());
  LOG(INFO) << "Received ListUsbDevices request";

  std::unique_ptr<dbus::Response> dbus_response(
      dbus::Response::FromMethodCall(method_call));

  dbus::MessageReader reader(method_call);
  dbus::MessageWriter writer(dbus_response.get());

  ListUsbDeviceRequest request;
  ListUsbDeviceResponse response;

  response.set_success(false);

  if (!reader.PopArrayOfBytesAsProto(&request)) {
    LOG(ERROR) << "Unable to parse ListUsbDeviceRequest from message";
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  auto iter = FindVm(request.owner_id(), request.vm_name());
  if (iter == vms_.end()) {
    LOG(ERROR) << "Requested VM does not exist";
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  std::vector<UsbDevice> usb_list;
  if (!iter->second->ListUsbDevice(&usb_list)) {
    LOG(ERROR) << "Failed to list USB devices";
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }
  for (auto usb : usb_list) {
    UsbDeviceMessage* usb_proto = response.add_usb_devices();
    usb_proto->set_guest_port(usb.port);
    usb_proto->set_vendor_id(usb.vid);
    usb_proto->set_product_id(usb.pid);
  }
  response.set_success(true);
  writer.AppendProtoAsArrayOfBytes(response);
  return dbus_response;
}

void Service::ComposeDnsResponse(dbus::MessageWriter* writer) {
  DnsSettings dns_settings;
  for (const auto& server : nameservers_) {
    dns_settings.add_nameservers(server);
  }
  for (const auto& domain : search_domains_) {
    dns_settings.add_search_domains(domain);
  }
  writer->AppendProtoAsArrayOfBytes(dns_settings);
}

std::unique_ptr<dbus::Response> Service::GetDnsSettings(
    dbus::MethodCall* method_call) {
  DCHECK(sequence_checker_.CalledOnValidSequence());
  LOG(INFO) << "Received GetDnsSettings request";

  std::unique_ptr<dbus::Response> dbus_response(
      dbus::Response::FromMethodCall(method_call));

  dbus::MessageWriter writer(dbus_response.get());
  ComposeDnsResponse(&writer);
  return dbus_response;
}

std::unique_ptr<dbus::Response> Service::SetVmCpuRestriction(
    dbus::MethodCall* method_call) {
  DCHECK(sequence_checker_.CalledOnValidSequence());
  VLOG(3) << "Received SetVmCpuRestriction request";

  std::unique_ptr<dbus::Response> dbus_response(
      dbus::Response::FromMethodCall(method_call));

  dbus::MessageReader reader(method_call);
  dbus::MessageWriter writer(dbus_response.get());

  SetVmCpuRestrictionRequest request;
  SetVmCpuRestrictionResponse response;

  if (!reader.PopArrayOfBytesAsProto(&request)) {
    LOG(ERROR) << "Unable to parse SetVmCpuRestrictionRequest from message";
    response.set_success(false);
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  bool success = false;
  const CpuRestrictionState state = request.cpu_restriction_state();
  switch (request.cpu_cgroup()) {
    case CPU_CGROUP_TERMINA:
      success = TerminaVm::SetVmCpuRestriction(state);
      break;
    case CPU_CGROUP_PLUGINVM:
      success = PluginVm::SetVmCpuRestriction(state);
      break;
    case CPU_CGROUP_ARCVM:
      success = ArcVm::SetVmCpuRestriction(state);
      break;
    default:
      LOG(ERROR) << "Unknown cpu_group";
      break;
  }

  response.set_success(success);
  writer.AppendProtoAsArrayOfBytes(response);
  return dbus_response;
}

void Service::OnResolvConfigChanged(std::vector<string> nameservers,
                                    std::vector<string> search_domains) {
  if (nameservers_ == nameservers && search_domains_ == search_domains) {
    // Only update guests if the nameservers and search domains changed.
    return;
  }

  nameservers_ = std::move(nameservers);
  search_domains_ = std::move(search_domains);

  for (auto& vm_entry : vms_) {
    auto& vm = vm_entry.second;
    if (vm->IsSuspended()) {
      // The VM is currently suspended and will not respond to RPCs.
      // SetResolvConfig() will be called when the VM resumes.
      continue;
    }
    vm->SetResolvConfig(nameservers_, search_domains_);
  }

  // Broadcast DnsSettingsChanged signal so Plugin VM dispatcher is aware as
  // well.
  dbus::Signal signal(kVmConciergeInterface, kDnsSettingsChangedSignal);
  dbus::MessageWriter writer(&signal);
  ComposeDnsResponse(&writer);
  exported_object_->SendSignal(&signal);
}

void Service::OnDefaultNetworkServiceChanged() {
  for (auto& vm_entry : vms_) {
    auto& vm = vm_entry.second;
    if (vm->IsSuspended()) {
      continue;
    }
    vm->HostNetworkChanged();
  }
}

void Service::NotifyCiceroneOfVmStarted(const VmId& vm_id,
                                        uint32_t cid,
                                        pid_t pid,
                                        std::string vm_token) {
  DCHECK(sequence_checker_.CalledOnValidSequence());
  dbus::MethodCall method_call(vm_tools::cicerone::kVmCiceroneInterface,
                               vm_tools::cicerone::kNotifyVmStartedMethod);
  dbus::MessageWriter writer(&method_call);
  vm_tools::cicerone::NotifyVmStartedRequest request;
  request.set_owner_id(vm_id.owner_id());
  request.set_vm_name(vm_id.name());
  request.set_cid(cid);
  request.set_vm_token(std::move(vm_token));
  request.set_pid(pid);
  writer.AppendProtoAsArrayOfBytes(request);
  std::unique_ptr<dbus::Response> dbus_response =
      cicerone_service_proxy_->CallMethodAndBlock(
          &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
  if (!dbus_response) {
    LOG(ERROR) << "Failed notifying cicerone of VM startup";
  }
}

void Service::SendVmStartedSignal(const VmId& vm_id,
                                  const vm_tools::concierge::VmInfo& vm_info,
                                  vm_tools::concierge::VmStatus status) {
  dbus::Signal signal(kVmConciergeInterface, kVmStartedSignal);
  vm_tools::concierge::VmStartedSignal proto;
  proto.set_owner_id(vm_id.owner_id());
  proto.set_name(vm_id.name());
  proto.mutable_vm_info()->CopyFrom(vm_info);
  proto.set_status(status);
  dbus::MessageWriter(&signal).AppendProtoAsArrayOfBytes(proto);
  exported_object_->SendSignal(&signal);
}

void Service::NotifyVmStopping(const VmId& vm_id, int64_t cid) {
  DCHECK(sequence_checker_.CalledOnValidSequence());
  // Notify cicerone.
  dbus::MethodCall method_call(vm_tools::cicerone::kVmCiceroneInterface,
                               vm_tools::cicerone::kNotifyVmStoppingMethod);
  dbus::MessageWriter writer(&method_call);
  vm_tools::cicerone::NotifyVmStoppingRequest request;
  request.set_owner_id(vm_id.owner_id());
  request.set_vm_name(vm_id.name());
  writer.AppendProtoAsArrayOfBytes(request);
  std::unique_ptr<dbus::Response> dbus_response =
      cicerone_service_proxy_->CallMethodAndBlock(
          &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
  if (!dbus_response) {
    LOG(ERROR) << "Failed notifying cicerone of stopping VM";
  }
}

void Service::NotifyVmStopped(const VmId& vm_id, int64_t cid) {
  DCHECK(sequence_checker_.CalledOnValidSequence());
  // Notify cicerone.
  dbus::MethodCall method_call(vm_tools::cicerone::kVmCiceroneInterface,
                               vm_tools::cicerone::kNotifyVmStoppedMethod);
  dbus::MessageWriter writer(&method_call);
  vm_tools::cicerone::NotifyVmStoppedRequest request;
  request.set_owner_id(vm_id.owner_id());
  request.set_vm_name(vm_id.name());
  writer.AppendProtoAsArrayOfBytes(request);
  std::unique_ptr<dbus::Response> dbus_response =
      cicerone_service_proxy_->CallMethodAndBlock(
          &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
  if (!dbus_response) {
    LOG(ERROR) << "Failed notifying cicerone of VM stopped";
  }

  // Send the D-Bus signal out to notify everyone that we have stopped a VM.
  dbus::Signal signal(kVmConciergeInterface, kVmStoppedSignal);
  vm_tools::concierge::VmStoppedSignal proto;
  proto.set_owner_id(vm_id.owner_id());
  proto.set_name(vm_id.name());
  proto.set_cid(cid);
  dbus::MessageWriter(&signal).AppendProtoAsArrayOfBytes(proto);
  exported_object_->SendSignal(&signal);
}

std::string Service::GetContainerToken(const VmId& vm_id,
                                       const std::string& container_name) {
  DCHECK(sequence_checker_.CalledOnValidSequence());
  dbus::MethodCall method_call(vm_tools::cicerone::kVmCiceroneInterface,
                               vm_tools::cicerone::kGetContainerTokenMethod);
  dbus::MessageWriter writer(&method_call);
  vm_tools::cicerone::ContainerTokenRequest request;
  vm_tools::cicerone::ContainerTokenResponse response;
  request.set_owner_id(vm_id.owner_id());
  request.set_vm_name(vm_id.name());
  request.set_container_name(container_name);
  writer.AppendProtoAsArrayOfBytes(request);
  std::unique_ptr<dbus::Response> dbus_response =
      cicerone_service_proxy_->CallMethodAndBlock(
          &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
  if (!dbus_response) {
    LOG(ERROR) << "Failed getting container token from cicerone";
    return "";
  }
  dbus::MessageReader reader(dbus_response.get());
  if (!reader.PopArrayOfBytesAsProto(&response)) {
    LOG(ERROR) << "Failed parsing proto response";
    return "";
  }
  return response.container_token();
}

void Service::OnTremplinStartedSignal(dbus::Signal* signal) {
  DCHECK_EQ(signal->GetInterface(), vm_tools::cicerone::kVmCiceroneInterface);
  DCHECK_EQ(signal->GetMember(), vm_tools::cicerone::kTremplinStartedSignal);

  vm_tools::cicerone::TremplinStartedSignal tremplin_started_signal;
  dbus::MessageReader reader(signal);
  if (!reader.PopArrayOfBytesAsProto(&tremplin_started_signal)) {
    LOG(ERROR) << "Failed to parse TremplinStartedSignal from DBus Signal";
    return;
  }

  auto iter = FindVm(tremplin_started_signal.owner_id(),
                     tremplin_started_signal.vm_name());
  if (iter == vms_.end()) {
    LOG(ERROR) << "Received signal from an unknown vm."
               << VmId(tremplin_started_signal.owner_id(),
                       tremplin_started_signal.vm_name());
    return;
  }
  LOG(INFO) << "Received TremplinStartedSignal for " << iter->first;
  iter->second->SetTremplinStarted();
}

void Service::OnVmToolsStateChangedSignal(dbus::Signal* signal) {
  string owner_id, vm_name;
  bool running;
  if (!pvm::dispatcher::ParseVmToolsChangedSignal(signal, &owner_id, &vm_name,
                                                  &running)) {
    return;
  }

  auto iter = FindVm(owner_id, vm_name);
  if (iter == vms_.end()) {
    LOG(ERROR) << "Received signal from an unknown vm "
               << VmId(owner_id, vm_name);
    return;
  }
  LOG(INFO) << "Received VmToolsStateChangedSignal for " << iter->first;
  iter->second->VmToolsStateChanged(running);
}

void Service::OnSignalConnected(const std::string& interface_name,
                                const std::string& signal_name,
                                bool is_connected) {
  if (!is_connected) {
    LOG(ERROR) << "Failed to connect to interface name: " << interface_name
               << " for signal " << signal_name;
  } else {
    LOG(INFO) << "Connected to interface name: " << interface_name
              << " for signal " << signal_name;
  }

  if (interface_name == vm_tools::cicerone::kVmCiceroneInterface) {
    DCHECK_EQ(signal_name, vm_tools::cicerone::kTremplinStartedSignal);
    is_tremplin_started_signal_connected_ = is_connected;
  }
}

void Service::HandleSuspendImminent() {
  for (const auto& pair : vms_) {
    auto& vm = pair.second;
    if (vm->UsesExternalSuspendSignals()) {
      continue;
    }
    vm->Suspend();
  }
}

void Service::HandleSuspendDone() {
  for (const auto& vm_entry : vms_) {
    auto& vm = vm_entry.second;
    if (vm->UsesExternalSuspendSignals()) {
      continue;
    }

    vm->Resume();

    string failure_reason;
    if (!vm->SetTime(&failure_reason)) {
      LOG(ERROR) << "Failed to set VM clock in " << vm_entry.first << ": "
                 << failure_reason;
    }

    vm->SetResolvConfig(nameservers_, search_domains_);
  }
}

Service::VmMap::iterator Service::FindVm(const std::string& owner_id,
                                         const std::string& vm_name) {
  auto it = vms_.find(VmId(owner_id, vm_name));
  // TODO(nverne): remove this fallback when Chrome is correctly seting owner_id
  if (it == vms_.end()) {
    return vms_.find(VmId("", vm_name));
  }
  return it;
}

}  // namespace concierge
}  // namespace vm_tools
