// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/logging.h>
#include <fuzzer/FuzzedDataProvider.h>
#include <session_manager/dbus-proxy-mocks.h>

#include "crash-reporter/chrome_collector.h"
#include "crash-reporter/paths.h"
#include "crash-reporter/test_util.h"

namespace {

class Environment {
 public:
  Environment() {
    // Disable logging per instructions.
    logging::SetMinLogLevel(logging::LOG_FATAL);
  }
};

bool g_is_feedback_allowed = false;
bool IsFeedbackAllowed() {
  return g_is_feedback_allowed;
}

class ChromeCollectorForFuzzing : public ChromeCollector {
 public:
  explicit ChromeCollectorForFuzzing(CrashSendingMode crash_sending_mode,
                                     std::string user_name,
                                     std::string user_hash)
      : ChromeCollector(crash_sending_mode),
        user_name_(std::move(user_name)),
        user_hash_(std::move(user_hash)) {}

  void SetUpDBus() override {
    // Mock out all DBus calls so (a) we don't actually call DBus and (b) we
    // don't CHECK fail when the DBus calls fail.
    auto mock =
        std::make_unique<org::chromium::SessionManagerInterfaceProxyMock>();
    test_util::SetActiveSessions(mock.get(), {{user_name_, user_hash_}});
    session_manager_proxy_ = std::move(mock);
  }

 private:
  // Results from the fake RetrieveActiveSessions call
  const std::string user_name_;
  const std::string user_hash_;
};

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  static Environment env;

  // Put all files into a per-run temp directory.
  base::ScopedTempDir temp_dir;
  CHECK(temp_dir.CreateUniqueTempDir());
  base::FilePath test_dir = temp_dir.GetPath();
  paths::SetPrefixForTesting(test_dir);

  FuzzedDataProvider provider(data, size);
  const int kArbitraryMaxNameLength = 4096;
  // Exactly one of exe_name and non_exe_error_key can be set or we CHECK fail.
  std::string exe_name;
  std::string non_exe_error_key;
  if (provider.ConsumeBool()) {
    exe_name = provider.ConsumeRandomLengthString(kArbitraryMaxNameLength);
  } else {
    non_exe_error_key =
        provider.ConsumeRandomLengthString(kArbitraryMaxNameLength);
  }
  pid_t pid = provider.ConsumeIntegral<pid_t>();
  uid_t uid = provider.ConsumeIntegral<uid_t>();
  if (exe_name.empty() || pid < (pid_t)0 || uid < (uid_t)0) {
    return 0;  // Or we'll CHECK-fail. Fuzzers shouldn't exit on any input.
  }
  g_is_feedback_allowed = provider.ConsumeBool();
  std::string user_name =
      provider.ConsumeRandomLengthString(kArbitraryMaxNameLength);
  std::string user_hash =
      provider.ConsumeRandomLengthString(kArbitraryMaxNameLength);

  // Despite the Memfd in the name of HandleCrashThroughMemfd, we can pass a
  // file handle to a normal file. memfd isn't supported by QEMU so better to
  // just use normal files here.
  base::FilePath test_input_path = test_dir.Append("test_input");
  std::string input = provider.ConsumeRemainingBytesAsString();
  base::WriteFile(test_input_path, input.c_str(), input.length());
  base::File test_input(test_input_path,
                        base::File::FLAG_OPEN | base::File::FLAG_READ);
  if (!test_input.IsValid()) {
    return 0;
  }

  // Empty because otherwise we CHECK-fail if this isn't a test image.
  const std::string kEmptyDumpDir;

  // kNormalCrashSendMode -- This makes it much simpler to mock out the DBus
  // calls, and we're not fuzzing the crash loop logic.
  ChromeCollectorForFuzzing collector(CrashCollector::kNormalCrashSendMode,
                                      std::move(user_name),
                                      std::move(user_hash));
  collector.Initialize(&IsFeedbackAllowed, false);
  collector.HandleCrashThroughMemfd(test_input.TakePlatformFile(), pid, uid,
                                    exe_name, non_exe_error_key, kEmptyDumpDir);
  return 0;
}
