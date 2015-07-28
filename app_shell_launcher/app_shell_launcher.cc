// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <grp.h>
#include <sys/resource.h>
#include <sys/types.h>

#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/memory/scoped_ptr.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <chromeos/ui/chromium_command_builder.h>
#include <chromeos/ui/util.h>
#include <chromeos/ui/x_server_runner.h>

using chromeos::ui::ChromiumCommandBuilder;
using chromeos::ui::XServerRunner;
using chromeos::ui::util::EnsureDirectoryExists;
using chromeos::ui::util::SetPermissions;

namespace {

// Authority file used for running the X server.
const char kXauthPath[] = "/var/run/x11.auth";

// Path to the app_shell binary.
const char kAppShellPath[] = "/opt/google/chrome/app_shell";

// Directory where data files are written at build time.
const char kReadonlyDataPath[] = "/usr/share/app_shell";

// Subdirectory under $DATA_DIR where user data should be stored.
const char kUserSubdir[] = "user";

// File in $DATA_DIR or kReadonlyDataPath containing the subdirectory name (not
// the full path) of the app to launch.
const char kMasterAppFile[] = "master_app";

// File in $DATA_DIR or kReadonlyDataPath containing the name of a preferred
// network to connect to.
const char kPreferredNetworkFile[] = "preferred_network";

// Subdirectory under $DATA_DIR or kReadonlyDataPath from which apps are loaded.
const char kAppsSubdir[] = "apps";

// Optional symlink in kReadonlyDataPath pointing to an executable to run
// instead of kAppShellPath.
const char kExecutableSymlink[] = "executable";

// Optional file declaring build-time modifications to app_shell's command line.
// See ChromiumCommandBuilder::ApplyUserConfig().
const char kConfigPath[] = "/etc/app_shell.conf";

// Optional file declaring developer modifications to app_shell's command line.
// See ChromiumCommandBuilder::ApplyUserConfig().
const char kDevConfigPath[] = "/etc/app_shell_dev.conf";

// Returns the first of the following paths that exists:
// - a file named |filename| within |stateful_dir|
// - a file named |filename| within |readonly_dir|
// - failing that, an empty path
base::FilePath GetDataPath(const base::FilePath& stateful_dir,
                           const base::FilePath& readonly_dir,
                           const std::string& filename) {
  base::FilePath stateful_path = stateful_dir.Append(filename);
  if (base::PathExists(stateful_path))
    return stateful_path;

  base::FilePath readonly_path = readonly_dir.Append(filename);
  if (base::PathExists(readonly_path))
    return readonly_path;

  return base::FilePath();
}

// Calls GetDataPath() and reads the file to |data_out|, trimming trailing
// whitespace. Returns true on success.
bool ReadData(const base::FilePath& stateful_dir,
              const base::FilePath& readonly_dir,
              const std::string& filename,
              std::string* data_out) {
  base::FilePath path = GetDataPath(stateful_dir, readonly_dir, filename);
  if (path.empty() || !base::ReadFileToString(path, data_out))
    return false;

  base::TrimWhitespaceASCII(*data_out, base::TRIM_TRAILING, data_out);
  return true;
}

// Optionally adds the --load-apps flag with list of apps to load, for example:
// --load-apps=/usr/share/app_shell/apps/foo,/usr/share/app_shell/apps/bar
void AddLoadAppsFlag(ChromiumCommandBuilder* builder) {
  const base::FilePath stateful_dir(builder->ReadEnvVar("DATA_DIR"));
  const base::FilePath readonly_dir(kReadonlyDataPath);

  // Look for an optional directory of unpacked apps.
  const base::FilePath apps_path =
      GetDataPath(stateful_dir, readonly_dir, kAppsSubdir);
  if (apps_path.empty())
    return;

  // Look for an optional preferences file with a master app subdirectory name.
  std::string master_app_name;
  ReadData(stateful_dir, readonly_dir, kMasterAppFile, &master_app_name);

  // Build a list of all subdirectories of the apps directory. If the master
  // app is found it goes at the front of the list.
  bool found_master = false;
  std::vector<std::string> apps_list;
  base::FileEnumerator dirs(apps_path, false /* recursive */,
                            base::FileEnumerator::DIRECTORIES);
  for (base::FilePath dir = dirs.Next(); !dir.empty(); dir = dirs.Next()) {
    if (!master_app_name.empty() && master_app_name == dir.BaseName().value()) {
      apps_list.insert(apps_list.begin(), dir.value());
      found_master = true;
    } else {
      apps_list.push_back(dir.value());
    }
  }

  // The developer probably intended to include at least one app.
  CHECK(!apps_list.empty()) << "No app subdirectories found.";

  // The developer probably wants the master app to be in the list of apps.
  if (!master_app_name.empty())
    CHECK(found_master) << "Master app " << master_app_name << " not found.";

  builder->AddArg("--load-apps=" + JoinString(apps_list, ','));
}

// Adds app_shell-specific flags.
void AddAppShellFlags(ChromiumCommandBuilder* builder) {
  const base::FilePath stateful_dir(builder->ReadEnvVar("DATA_DIR"));
  const base::FilePath readonly_dir(kReadonlyDataPath);

  // Set --data-path to tell app_shell where to store user data.
  const base::FilePath user_path = stateful_dir.Append(kUserSubdir);
  CHECK(EnsureDirectoryExists(user_path, builder->uid(), builder->gid(), 0700));
  builder->AddArg("--data-path=" + user_path.value());

  std::string network;
  if (ReadData(stateful_dir, readonly_dir, kPreferredNetworkFile, &network))
    builder->AddArg("--app-shell-preferred-network=" + network);

  AddLoadAppsFlag(builder);
}

// Replaces the currently-running process with app_shell.
void ExecAppShell(const ChromiumCommandBuilder& builder) {
  PCHECK(initgroups(ChromiumCommandBuilder::kUser, builder.gid()) == 0);
  PCHECK(setgid(builder.gid()) == 0);
  PCHECK(setuid(builder.uid()) == 0);

  PCHECK(clearenv() == 0);
  for (ChromiumCommandBuilder::StringMap::const_iterator it =
           builder.environment_variables().begin();
       it != builder.environment_variables().end(); ++it) {
    PCHECK(setenv(it->first.c_str(), it->second.c_str(), 1) == 0);
  }

  const size_t kMaxArgs = 64;
  CHECK_LE(builder.arguments().size(), kMaxArgs);

  // One extra pointer for the initial command and one for the terminating NULL.
  char* argv[kMaxArgs + 2];
  for (size_t i = 0; i < arraysize(argv); ++i)
    argv[i] = nullptr;

  // Check for a symlink in the readonly dir pointing to an alternate
  // executable.
  std::string exec_path = kAppShellPath;
  base::FilePath link_dest;
  if (base::ReadSymbolicLink(
          base::FilePath(kReadonlyDataPath).Append(kExecutableSymlink),
          &link_dest)) {
    exec_path = link_dest.value();
  }

  argv[0] = const_cast<char*>(exec_path.c_str());
  for (size_t i = 0; i < builder.arguments().size(); ++i)
    argv[i + 1] = const_cast<char*>(builder.arguments()[i].c_str());

  LOG(INFO) << "Exec-ing " << exec_path << " "
            << JoinString(builder.arguments(), ' ');
  PCHECK(execv(argv[0], argv) == 0) << "Couldn't exec " << argv[0];
}

}  // namespace

int main(int argc, char** argv) {
  ChromiumCommandBuilder builder;
  CHECK(builder.Init());

  // Start the X server in the background before doing more-expensive setup.
  scoped_ptr<XServerRunner> x_runner;
  const base::FilePath xauth_path(kXauthPath);
  const bool using_x11 = builder.UseFlagIsSet("X");
  if (using_x11) {
    x_runner.reset(new XServerRunner);
    CHECK(x_runner->StartServer(
        XServerRunner::kDefaultUser, XServerRunner::kDefaultVt,
        builder.is_developer_end_user(), xauth_path));
  }

  builder.SetUpChromium(using_x11 ? xauth_path : base::FilePath());
  builder.EnableCoreDumps();
  AddAppShellFlags(&builder);

  const base::FilePath config_path(kConfigPath);
  if (base::PathExists(config_path))
    builder.ApplyUserConfig(config_path);

  const base::FilePath dev_config_path(kDevConfigPath);
  if (builder.is_developer_end_user() && base::PathExists(dev_config_path))
    builder.ApplyUserConfig(dev_config_path);

  if (using_x11)
    CHECK(x_runner->WaitForServer());

  // Do not add setup code below this point. Potentially-expensive work should
  // be done between StartServer() and WaitForServer().

  // This call never returns.
  ExecAppShell(builder);
  return 1;
}
