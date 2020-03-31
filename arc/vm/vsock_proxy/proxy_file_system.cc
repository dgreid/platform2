// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/vm/vsock_proxy/proxy_file_system.h"

#include <errno.h>

#include <string>
#include <utility>

#include <base/bind.h>
#include <base/logging.h>
#include <base/optional.h>
#include <base/posix/eintr_wrapper.h>
#include <base/stl_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_piece.h>
#include <base/synchronization/waitable_event.h>
#include <base/task_runner.h>
#include <base/threading/thread_task_runner_handle.h>

#include "arc/vm/vsock_proxy/fuse_mount.h"

namespace arc {
namespace {

constexpr char kFileSystemName[] = "arcvm-serverproxy";

// Returns ProxyFileSystem assigned to the FUSE's private_data.
ProxyFileSystem* GetFileSystem() {
  return static_cast<ProxyFileSystem*>(fuse_get_context()->private_data);
}

int GetAttr(const char* path, struct stat* stat) {
  return GetFileSystem()->GetAttr(path, stat);
}

int Open(const char* path, struct fuse_file_info* fi) {
  return GetFileSystem()->Open(path, fi);
}

int Read(const char* path,
         char* buf,
         size_t size,
         off_t off,
         struct fuse_file_info* fi) {
  return GetFileSystem()->Read(path, buf, size, off, fi);
}

int Release(const char* path, struct fuse_file_info* fi) {
  return GetFileSystem()->Release(path, fi);
}

int ReadDir(const char* path,
            void* buf,
            fuse_fill_dir_t filler,
            off_t offset,
            struct fuse_file_info* fi) {
  return GetFileSystem()->ReadDir(path, buf, filler, offset, fi);
}

// Parses the given path to a handle. The path should be formatted in
// "/<handle>", where <handle> is int64_t. Returns nullopt on error.
base::Optional<int64_t> ParseHandle(const char* path) {
  if (!path || path[0] != '/')
    return base::nullopt;

  // Parse the path as int64_t excluding leading '/'.
  int64_t value = 0;
  if (!base::StringToInt64(path + 1, &value))
    return base::nullopt;

  return value;
}

}  // namespace

ProxyFileSystem::ProxyFileSystem(
    Delegate* delegate,
    scoped_refptr<base::TaskRunner> delegate_task_runner,
    const base::FilePath& mount_path)
    : delegate_(delegate),
      delegate_task_runner_(delegate_task_runner),
      mount_path_(mount_path) {}

ProxyFileSystem::~ProxyFileSystem() {
  if (init_task_runner_) {
    base::WaitableEvent stopped(
        base::WaitableEvent::ResetPolicy::MANUAL,
        base::WaitableEvent::InitialState::NOT_SIGNALED);
    init_task_runner_->PostTask(
        FROM_HERE,
        base::BindOnce(
            [](ProxyFileSystem* proxy_file_system, base::WaitableEvent* done) {
              proxy_file_system->fuse_mount_ = nullptr;
              done->Signal();
            },
            base::Unretained(this), &stopped));
    stopped.Wait();
  }
}

bool ProxyFileSystem::Init() {
  DCHECK(!init_task_runner_) << "Init can only be called once.";
  init_task_runner_ = base::ThreadTaskRunnerHandle::Get();

  const std::string path_str = mount_path_.value();
  const char* fuse_argv[] = {
      "",  // Dummy argv[0].
      // Never cache attr/dentry since our backend storage is not exclusive to
      // this process.
      "-o",
      "attr_timeout=0",
      "-o",
      "entry_timeout=0",
      "-o",
      "negative_timeout=0",
      "-o",
      "ac_attr_timeout=0",
      "-o",
      "direct_io",
  };

  constexpr struct fuse_operations operations = {
      .getattr = arc::GetAttr,
      .open = arc::Open,
      .read = arc::Read,
      .release = arc::Release,
      .readdir = arc::ReadDir,
  };
  fuse_mount_ = std::make_unique<FuseMount>(mount_path_, kFileSystemName);
  if (!fuse_mount_->Init(base::size(fuse_argv), const_cast<char**>(fuse_argv),
                         operations, this)) {
    return false;
  }
  // TODO(hidehiko): Drop CAPS_SYS_ADMIN with minijail setup.
  return true;
}

int ProxyFileSystem::GetAttr(const char* path, struct stat* stat) {
  if (path == base::StringPiece("/")) {
    stat->st_mode = S_IFDIR;
    stat->st_nlink = 2;
    return 0;
  }

  auto handle = ParseHandle(path);
  if (!handle.has_value()) {
    LOG(ERROR) << "Invalid path: " << path;
    return -ENOENT;
  }

  auto state = GetState(handle.value());
  if (!state.has_value()) {
    LOG(ERROR) << "Handle not found: " << path;
    return -ENOENT;
  }

  stat->st_mode = S_IFREG;
  stat->st_nlink = 1;
  if (state.value() == State::NOT_OPENED) {
    // If the file is not opened yet, this is called from kernel to open the
    // file, which is initiated by the open(2) called in RegisterHandle()
    // on |delegate_task_runner_|.
    // Thus, we cannot make a blocking call to retrieve the size of the file,
    // because it causes deadlock. Instead, we just fill '0', and return
    // immediately.
    stat->st_size = 0;
    return 0;
  }
  base::WaitableEvent event(base::WaitableEvent::ResetPolicy::MANUAL,
                            base::WaitableEvent::InitialState::NOT_SIGNALED);
  int return_value = -EIO;
  delegate_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&ProxyFileSystem::GetAttrInternal, base::Unretained(this),
                     &event, handle.value(), &return_value, &stat->st_size));
  event.Wait();
  return return_value;
}

void ProxyFileSystem::GetAttrInternal(base::WaitableEvent* event,
                                      int64_t handle,
                                      int* return_value,
                                      off_t* size) {
  delegate_->Fstat(handle,
                   base::BindOnce(
                       [](base::WaitableEvent* event, int* return_value,
                          off_t* out_size, int error_code, int64_t size) {
                         *return_value = -error_code;
                         if (error_code == 0)
                           *out_size = static_cast<off_t>(size);
                         event->Signal();
                       },
                       event, return_value, size));
}

int ProxyFileSystem::Open(const char* path, struct fuse_file_info* fi) {
  auto handle = ParseHandle(path);
  if (!handle.has_value()) {
    LOG(ERROR) << "Invalid path: " << path;
    return -ENOENT;
  }

  {
    base::AutoLock lock(handle_map_lock_);
    auto iter = handle_map_.find(handle.value());
    if (iter == handle_map_.end()) {
      LOG(ERROR) << "Handle not found: " << path;
      return -ENOENT;
    }
    iter->second = State::OPENED;
  }

  return 0;
}

int ProxyFileSystem::Read(const char* path,
                          char* buf,
                          size_t size,
                          off_t off,
                          struct fuse_file_info* fi) {
  auto handle = ParseHandle(path);
  if (!handle.has_value()) {
    LOG(ERROR) << "Invalid path: " << path;
    return -ENOENT;
  }

  if (!GetState(handle.value()).has_value()) {
    LOG(ERROR) << "Handle not found: " << path;
    return -ENOENT;
  }

  base::WaitableEvent event(base::WaitableEvent::ResetPolicy::MANUAL,
                            base::WaitableEvent::InitialState::NOT_SIGNALED);
  int return_value = -EIO;
  delegate_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&ProxyFileSystem::ReadInternal, base::Unretained(this),
                     &event, handle.value(), buf, size, off, &return_value));
  event.Wait();
  return return_value;
}

void ProxyFileSystem::ReadInternal(base::WaitableEvent* event,
                                   int64_t handle,
                                   char* buf,
                                   size_t size,
                                   off_t off,
                                   int* return_value) {
  delegate_->Pread(
      handle, size, off,
      base::BindOnce(
          [](base::WaitableEvent* event, char* buf, int* return_value,
             int error_code, const std::string& blob) {
            if (error_code != 0) {
              *return_value = -error_code;
            } else {
              memcpy(buf, blob.data(), blob.size());
              *return_value = static_cast<int>(blob.size());
            }
            event->Signal();
          },
          event, buf, return_value));
}

int ProxyFileSystem::Release(const char* path, struct fuse_file_info* fi) {
  auto handle = ParseHandle(path);
  if (!handle.has_value()) {
    LOG(ERROR) << "Invalid path: " << path;
    return -ENOENT;
  }

  {
    base::AutoLock lock(handle_map_lock_);
    if (handle_map_.erase(handle.value()) == 0) {
      LOG(ERROR) << "Handle not found: " << path;
      return -ENOENT;
    }
  }

  // |this| outlives |delegate_task_runner_|, so passing raw |this| pointer here
  // is safe.
  delegate_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce([](ProxyFileSystem* self,
                        int64_t handle) { self->delegate_->Close(handle); },
                     this, handle.value()));
  return 0;
}

int ProxyFileSystem::ReadDir(const char* path,
                             void* buf,
                             fuse_fill_dir_t filler,
                             off_t offset,
                             struct fuse_file_info* fi) {
  // Just returns as if it is empty directory.
  filler(buf, ".", nullptr, 0);
  filler(buf, "..", nullptr, 0);
  return 0;
}

base::ScopedFD ProxyFileSystem::RegisterHandle(int64_t handle) {
  {
    base::AutoLock lock(handle_map_lock_);
    if (!handle_map_.emplace(handle, State::NOT_OPENED).second) {
      LOG(ERROR) << "The handle was already registered: " << handle;
      return {};
    }
  }

  // Currently read-only file descriptor is only supported.
  return base::ScopedFD(HANDLE_EINTR(
      open(mount_path_.Append(base::Int64ToString(handle)).value().c_str(),
           O_RDONLY | O_CLOEXEC)));
}

base::Optional<ProxyFileSystem::State> ProxyFileSystem::GetState(
    int64_t handle) {
  base::AutoLock lock_(handle_map_lock_);
  auto iter = handle_map_.find(handle);
  if (iter == handle_map_.end())
    return base::nullopt;
  return iter->second;
}

}  // namespace arc
