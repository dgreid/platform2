// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "login_manager/chrome_setup.h"

#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

#include <base/file_util.h>
#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/memory/scoped_ptr.h>
#include <base/strings/stringprintf.h>
#include <chromeos/ui/chromium_command_builder.h>
#include <chromeos/ui/util.h>
#include <chromeos/ui/x_server_runner.h>

using chromeos::ui::ChromiumCommandBuilder;
using chromeos::ui::XServerRunner;
using chromeos::ui::util::EnsureDirectoryExists;
using chromeos::ui::util::SetPermissions;

namespace login_manager {

namespace {

// User, VT, and authority file used for running the X server.
const char kXorgUser[] = "xorg";
const int kXorgVt = 1;
const char kXauthFile[] = "/var/run/chromelogin.auth";

// Path to file containing developer-supplied modifications to Chrome's
// environment and command line. Passed to
// ChromiumCommandBuilder::ApplyUserConfig().
const char kChromeDevConfigPath[] = "/etc/chrome_dev.conf";

// Returns a base::FilePath corresponding to the DATA_DIR environment variable.
base::FilePath GetDataDir(ChromiumCommandBuilder* builder) {
  return base::FilePath(builder->ReadEnvVar("DATA_DIR"));
}

// Returns a base::FilePath corresponding to the subdirectory of DATA_DIR where
// user data is stored.
base::FilePath GetUserDir(ChromiumCommandBuilder* builder) {
  return base::FilePath(GetDataDir(builder).Append("user"));
}

// Called by AddUiFlags() to take a wallpaper flag type ("default" or "guest"
// and file type (e.g. "default", "oem", "guest") and add the corresponding
// flags to |builder| if the files exist. Returns false if the files don't
// exist.
bool AddWallpaperFlags(ChromiumCommandBuilder* builder,
                       const std::string& flag_type,
                       const std::string& file_type) {
  const base::FilePath large_path(base::StringPrintf(
      "/usr/share/chromeos-assets/wallpaper/%s_large.jpg", file_type.c_str()));
  const base::FilePath small_path(base::StringPrintf(
      "/usr/share/chromeos-assets/wallpaper/%s_small.jpg", file_type.c_str()));
  if (!base::PathExists(large_path) || !base::PathExists(small_path))
    return false;

  builder->AddArg(base::StringPrintf("--ash-%s-wallpaper-large=%s",
      flag_type.c_str(), large_path.value().c_str()));
  builder->AddArg(base::StringPrintf("--ash-%s-wallpaper-small=%s",
      flag_type.c_str(), small_path.value().c_str()));
  return true;
}

// Ensures that necessary directory exist with the correct permissions and sets
// related arguments and environment variables.
void CreateDirectories(ChromiumCommandBuilder* builder) {
  const uid_t uid = builder->uid();
  const gid_t gid = builder->gid();
  const uid_t kRootUid = 0;
  const gid_t kRootGid = 0;

  const base::FilePath data_dir = GetDataDir(builder);
  builder->AddArg("--user-data-dir=" + data_dir.value());

  const base::FilePath user_dir = GetUserDir(builder);
  CHECK(EnsureDirectoryExists(user_dir, uid, gid, 0755));
  // TODO(keescook): Remove Chrome's use of $HOME.
  builder->AddEnvVar("HOME", user_dir.value());

  // Old builds will have a profile dir that's owned by root; newer ones won't
  // have this directory at all.
  CHECK(EnsureDirectoryExists(data_dir.Append("Default"), uid, gid, 0755));

  // TODO(cmasone,derat): Stop using this directory and delete this code.
  const base::FilePath state_dir("/var/run/state");
  CHECK(base::DeleteFile(state_dir, true));
  CHECK(EnsureDirectoryExists(state_dir, kRootUid, kRootGid, 0710));

  // Create a directory where the session manager can store a copy of the user
  // policy key, that will be readable by the chrome process as chronos.
  const base::FilePath policy_dir("/var/run/user_policy");
  CHECK(base::DeleteFile(policy_dir, true));
  CHECK(EnsureDirectoryExists(policy_dir, kRootUid, gid, 0710));

  // Create a directory where the chrome process can store a reboot request so
  // that it persists across browser crashes but is always removed on reboot.
  CHECK(EnsureDirectoryExists(
      base::FilePath("/var/run/chrome"), uid, gid, 0700));

  // Ensure the existence of the directory in which the whitelist and other
  // ownership-related state will live. Yes, it should be owned by root. The
  // permissions are set such that the chronos user can see the content of known
  // files inside whitelist, but not anything else.
  CHECK(EnsureDirectoryExists(
      base::FilePath("/var/lib/whitelist"), kRootUid, gid, 0710));

  // Create the directory where external data referenced by policies is cached
  // for device-local accounts. This data is read and written by chronos.
  CHECK(EnsureDirectoryExists(
      base::FilePath("/var/cache/device_local_account_external_policy_data"),
      uid, gid, 0700));

  // Create the directory where the AppPack extensions are cached.
  // These extensions are read and written by chronos.
  CHECK(EnsureDirectoryExists(
      base::FilePath("/var/cache/app_pack"), uid, gid, 0700));

  // Create the directory where extensions for device-local accounts are cached.
  // These extensions are read and written by chronos.
  CHECK(EnsureDirectoryExists(
      base::FilePath("/var/cache/device_local_account_extensions"),
      uid, gid, 0700));

  // Create the directory for shared installed extensions.
  // Shared extensions are validated at runtime by the browser.
  // These extensions are read and written by chronos.
  CHECK(EnsureDirectoryExists(
      base::FilePath("/var/cache/shared_extensions"), uid, gid, 0700));

  // Enable us to keep track of the user's chosen time zone.
  // Default to Pacific if we don't have one set.
  const base::FilePath timezone_file("/var/lib/timezone/localtime");
  // TODO(derat): Move this back into the !base::PathExists() block in M39 or
  // later, after http://crbug.com/390188 has had time to be cleaned up.
  CHECK(EnsureDirectoryExists(timezone_file.DirName(), uid, gid, 0755));
  if (!base::PathExists(timezone_file)) {
    CHECK(base::CreateSymbolicLink(
        base::FilePath("/usr/share/zoneinfo/US/Pacific"), timezone_file));
  }

  // Tell Chrome where to write logging messages before the user logs in.
  base::FilePath system_log_dir("/var/log/chrome");
  CHECK(EnsureDirectoryExists(system_log_dir, uid, gid, 0755));
  builder->AddEnvVar("CHROME_LOG_FILE",
      system_log_dir.Append("chrome").value());

  // Log directory for the user session. Note that the user dir won't be mounted
  // until later (when the cryptohome is mounted), so we don't create
  // CHROMEOS_SESSION_LOG_DIR here.
  builder->AddEnvVar("CHROMEOS_SESSION_LOG_DIR",
      user_dir.Append("log").value());
}

// Creates crash-handling-related directories and adds related arguments.
void InitCrashHandling(ChromiumCommandBuilder* builder) {
  const base::FilePath user_dir = GetUserDir(builder);
  const uid_t uid = builder->uid();
  const gid_t gid = builder->gid();

  // Force Chrome minidumps that are sent to the crash server to also be written
  // locally. Chrome creates these files in
  // ~/.config/google-chrome/Crash Reports/.
  const base::FilePath stateful_etc("/mnt/stateful_partition/etc");
  if (base::PathExists(stateful_etc.Append("enable_chromium_minidumps"))) {
    builder->AddEnvVar("CHROME_HEADLESS", "1");
    const base::FilePath reports_dir(
        user_dir.Append(".config/google-chrome/Crash Reports"));
    if (!base::PathExists(reports_dir)) {
      base::FilePath minidump_dir("/var/minidumps");
      EnsureDirectoryExists(minidump_dir, uid, gid, 0700);
      EnsureDirectoryExists(reports_dir.DirName(), uid, gid, 0700);
      base::CreateSymbolicLink(minidump_dir, reports_dir);
    }
  }

  // Enable gathering of core dumps via a file in the stateful partition so it
  // can be enabled post-build.
  if (base::PathExists(stateful_etc.Append("enable_chromium_coredumps"))) {
    EnsureDirectoryExists(base::FilePath("/var/coredumps"), uid, gid, 0700);
    struct rlimit limit;
    limit.rlim_cur = limit.rlim_max = RLIM_INFINITY;
    if (setrlimit(RLIMIT_CORE, &limit) != 0)
      PLOG(ERROR) << "Setting unlimited coredumps with setrlimit() failed";
    const std::string kPattern("/var/coredumps/core.%e.%p");
    base::WriteFile(base::FilePath("/proc/sys/kernel/core_pattern"),
                    kPattern.c_str(), kPattern.size());
  }
}

// Adds system-related flags to the command line.
void AddSystemFlags(ChromiumCommandBuilder* builder) {
  const base::FilePath data_dir = GetDataDir(builder);

  // We need to delete these files as Chrome may have left them around from its
  // prior run (if it crashed).
  base::DeleteFile(data_dir.Append("SingletonLock"), false);
  base::DeleteFile(data_dir.Append("SingletonSocket"), false);

  builder->AddArg("--max-unused-resource-memory-usage-percentage=5");

  // On developer systems, set a flag to let the browser know.
  if (builder->is_developer_end_user())
    builder->AddArg("--system-developer-mode");
}

// Adds UI-related flags to the command line.
void AddUiFlags(ChromiumCommandBuilder* builder, bool using_x11) {
  const base::FilePath data_dir = GetDataDir(builder);

  // Force OOBE on test images that have requested it.
  if (base::PathExists(base::FilePath("/root/.test_repeat_oobe"))) {
    base::DeleteFile(data_dir.Append(".oobe_completed"), false);
    base::DeleteFile(data_dir.Append("Local State"), false);
  }

  if (using_x11) {
    const base::FilePath user_xauth_file(data_dir.Append(".Xauthority"));
    PCHECK(base::CopyFile(base::FilePath(kXauthFile), user_xauth_file))
        << "Unable to copy " << kXauthFile << " to " << user_xauth_file.value();
    CHECK(SetPermissions(
        user_xauth_file, builder->uid(), builder->gid(), 0600));
    builder->AddEnvVar("XAUTHORITY", user_xauth_file.value());
    builder->AddEnvVar("DISPLAY", ":0.0");
  }

  builder->AddArg("--login-manager");
  builder->AddArg("--login-profile=user");

  if (builder->UseFlagIsSet("natural_scroll_default"))
    builder->AddArg("--enable-natural-scroll-default");
  if (!builder->UseFlagIsSet("legacy_keyboard"))
    builder->AddArg("--has-chromeos-keyboard");
  if (builder->UseFlagIsSet("has_diamond_key"))
    builder->AddArg("--has-chromeos-diamond-key");

  if (builder->UseFlagIsSet("legacy_power_button"))
    builder->AddArg("--aura-legacy-power-button");

  if (builder->UseFlagIsSet("disable_login_animations")) {
    builder->AddArg("--disable-login-animations");
    builder->AddArg("--disable-boot-animation");
    builder->AddArg("--ash-copy-host-background-at-boot");
  } else if (builder->UseFlagIsSet("fade_boot_splash_screen")) {
    builder->AddArg("--ash-animate-from-boot-splash-screen");
  }

  if (AddWallpaperFlags(builder, "default", "oem"))
    builder->AddArg("--ash-default-wallpaper-is-oem");
  else
    AddWallpaperFlags(builder, "default", "default");
  AddWallpaperFlags(builder, "guest", "guest");

  // TODO(yongjaek): Remove the following flag when the kiosk mode app is ready
  // at crbug.com/309806.
  if (builder->UseFlagIsSet("moblab"))
    builder->AddArg("--disable-demo-mode");
}

// Adds enterprise-related flags to the command line.
void AddEnterpriseFlags(ChromiumCommandBuilder* builder) {
  // Device Manager Server used to fetch the enterprise policy, if applicable.
  builder->AddArg(
      "--device-management-url=https://m.google.com/devicemanagement/data/api");

  builder->AddArg("--enterprise-enable-forced-re-enrollment");
  builder->AddArg("--enterprise-enrollment-initial-modulus=9");
  builder->AddArg("--enterprise-enrollment-modulus-limit=13");
}

// Adds patterns to the --vmodule flag.
void AddVmodulePatterns(ChromiumCommandBuilder* builder) {
  // There has been a steady supply of bug reports about screen locking. These
  // messages are useful for determining what happened within feedback reports.
  builder->AddVmodulePattern("screen_locker=1");
  builder->AddVmodulePattern("webui_screen_locker=1");

  // TODO(ygorshenin): Remove this once we will have logs from places where
  // shill was tested (crosbug.com/36622).
  builder->AddVmodulePattern("network_portal_detector_impl=1");

  // TODO(nkostylev): Remove this one once Rollback UI on login screen
  // is launched. http://crbug.com/382459
  builder->AddVmodulePattern("reset_screen_handler=1");
  builder->AddVmodulePattern("update_engine_client=1");

  // Turn on logging about external displays being connected and disconnected.
  // Different behavior is seen from different displays and these messages are
  // used to determine what happened within feedback reports.
  builder->AddVmodulePattern("*ui/display/chromeos*=1");
  builder->AddVmodulePattern("*ash/display*=1");

  // Turn on plugin loading failure logging for crbug.com/314301.
  builder->AddVmodulePattern("*zygote*=1");
  builder->AddVmodulePattern("*plugin*=2");
}

}  // namespace

void PerformChromeSetup(std::map<std::string, std::string>* env_vars_out,
                        std::vector<std::string>* args_out,
                        uid_t* uid_out) {
  DCHECK(env_vars_out);
  DCHECK(args_out);
  DCHECK(uid_out);

  ChromiumCommandBuilder builder;
  CHECK(builder.Init());

  // Start X in the background before doing more-expensive setup.
  scoped_ptr<XServerRunner> x_runner;
  const bool using_x11 = builder.UseFlagIsSet("X");
  if (using_x11) {
    x_runner.reset(new XServerRunner);
    CHECK(x_runner->StartServer(
        kXorgUser, kXorgVt, builder.is_developer_end_user(),
        base::FilePath(kXauthFile)));
  }

  builder.SetUpChromium();

  // Please add new code to the most-appropriate helper function instead of
  // putting it here. Things that to all Chromium-derived binaries (e.g.
  // app_shell, content_shell, etc.) rather than just to Chrome belong in the
  // ChromiumCommandBuilder class instead.
  CreateDirectories(&builder);
  InitCrashHandling(&builder);
  AddSystemFlags(&builder);
  AddUiFlags(&builder, using_x11);
  AddEnterpriseFlags(&builder);
  AddVmodulePatterns(&builder);

  // Apply any modifications requested by the developer.
  if (builder.is_developer_end_user())
    builder.ApplyUserConfig(base::FilePath(kChromeDevConfigPath));

  *env_vars_out = builder.environment_variables();
  *args_out = builder.arguments();
  *uid_out = builder.uid();

  if (using_x11)
    CHECK(x_runner->WaitForServer());

  // Do not add code here. Potentially-expensive work should be done between
  // StartServer() and WaitForServer().
}

}  // namespace login_manager
