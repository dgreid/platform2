// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/vm/vsock_proxy/proxy_file_system.h"

#include <unistd.h>

#include <algorithm>
#include <memory>
#include <string>
#include <utility>

#include <base/bind.h>
#include <base/bind_helpers.h>
#include <base/files/file_path.h>
#include <base/files/scoped_file.h>
#include <base/files/scoped_temp_dir.h>
#include <base/posix/eintr_wrapper.h>
#include <base/synchronization/waitable_event.h>
#include <base/test/task_environment.h>
#include <base/threading/thread.h>
#include <gtest/gtest.h>

namespace arc {
namespace {

constexpr int64_t kHandle = 123;
constexpr char kTestData[] = "abcdefghijklmnopqrstuvwxyz";

class ProxyFileSystemTest : public testing::Test,
                            public ProxyFileSystem::Delegate {
 public:
  ProxyFileSystemTest() = default;
  ~ProxyFileSystemTest() override = default;
  ProxyFileSystemTest(const ProxyFileSystemTest&) = delete;
  ProxyFileSystemTest& operator=(const ProxyFileSystemTest&) = delete;

  void SetUp() override {
    ASSERT_TRUE(mount_dir_.CreateUniqueTempDir());

    ASSERT_TRUE(delegate_thread_.Start());

    base::Thread::Options options(base::MessagePumpType::IO, 0);
    ASSERT_TRUE(file_system_thread_.StartWithOptions(options));

    file_system_ = std::make_unique<ProxyFileSystem>(
        this, delegate_thread_.task_runner(), mount_dir_.GetPath());
    bool result = false;
    file_system_thread_.task_runner()->PostTask(
        FROM_HERE,
        base::BindOnce([](ProxyFileSystem* file_system,
                          bool* result) { *result = file_system->Init(); },
                       file_system_.get(), &result));
    file_system_thread_.FlushForTesting();
    ASSERT_TRUE(result);
  }

  // ProxyFileSystem::Delegate overridess:
  void Pread(int64_t handle,
             uint64_t count,
             uint64_t offset,
             PreadCallback callback) override {
    if (handle == kHandle) {
      const std::string data(kTestData);
      offset = std::min(static_cast<uint64_t>(data.size()), offset);
      count = std::min(static_cast<uint64_t>(data.size()) - offset, count);
      std::move(callback).Run(0, data.substr(offset, count));
    } else {
      std::move(callback).Run(EBADF, std::string());
    }
  }
  void Close(int64_t handle) override {
    EXPECT_FALSE(close_was_called_.IsSignaled());
    EXPECT_EQ(kHandle, handle);
    close_was_called_.Signal();
  }
  void Fstat(int64_t handle, FstatCallback callback) override {
    if (handle == kHandle) {
      std::move(callback).Run(0, sizeof(kTestData));
    } else {
      std::move(callback).Run(EBADF, 0);
    }
  }

 protected:
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::ThreadingMode::MAIN_THREAD_ONLY,
      base::test::TaskEnvironment::MainThreadType::IO};

  // Mount point for ProxyFileSystem.
  base::ScopedTempDir mount_dir_;

  base::Thread delegate_thread_{"FileSystemDelegate"};
  base::Thread file_system_thread_{"FileSystem"};

  // ProxyFileSystem to be tested.
  std::unique_ptr<ProxyFileSystem> file_system_;

  base::WaitableEvent close_was_called_{
      base::WaitableEvent::ResetPolicy::MANUAL,
      base::WaitableEvent::InitialState::NOT_SIGNALED};
};

// On ARM devices, unit tests run without necessary capabilities in QEMU.
#if defined(ARCH_CPU_ARM_FAMILY)
#define MAYBE_RegularFileReadTest DISABLED_RegularFileReadTest
#else
#define MAYBE_RegularFileReadTest RegularFileReadTest
#endif

TEST_F(ProxyFileSystemTest, MAYBE_RegularFileReadTest) {
  base::ScopedFD fd = file_system_->RegisterHandle(kHandle);
  char buf[10];
  ASSERT_EQ(sizeof(buf), HANDLE_EINTR(read(fd.get(), buf, sizeof(buf))));
  EXPECT_EQ("abcdefghij", std::string(buf, sizeof(buf)));
  ASSERT_EQ(sizeof(buf), HANDLE_EINTR(read(fd.get(), buf, sizeof(buf))));
  EXPECT_EQ("klmnopqrst", std::string(buf, sizeof(buf)));
  ASSERT_EQ(6, HANDLE_EINTR(read(fd.get(), buf, sizeof(buf))));
  EXPECT_EQ("uvwxyz", std::string(buf, 6));
  // Make sure EOF.
  ASSERT_EQ(0, HANDLE_EINTR(read(fd.get(), buf, sizeof(buf))));

  // Close the file descriptor.
  fd.reset();
  close_was_called_.Wait();
}

}  // namespace
}  // namespace arc
