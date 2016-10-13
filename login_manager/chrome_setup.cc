// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "login_manager/chrome_setup.h"

#include <sys/stat.h>
#include <unistd.h>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
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

// Authority file used for running the X server.
const char kXauthPath[] = "/var/run/chromelogin.auth";

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
// or "child") and file type (e.g. "child", "default", "oem", "guest") and
// add the corresponding flags to |builder| if the files exist. Returns false
// if the files don't exist.
bool AddWallpaperFlags(ChromiumCommandBuilder* builder,
                       const std::string& flag_type,
                       const std::string& file_type) {
  const base::FilePath large_path(base::StringPrintf(
      "/usr/share/chromeos-assets/wallpaper/%s_large.jpg", file_type.c_str()));
  const base::FilePath small_path(base::StringPrintf(
      "/usr/share/chromeos-assets/wallpaper/%s_small.jpg", file_type.c_str()));
  if (!base::PathExists(large_path) || !base::PathExists(small_path))
    return false;

  builder->AddArg(base::StringPrintf("--%s-wallpaper-large=%s",
      flag_type.c_str(), large_path.value().c_str()));
  builder->AddArg(base::StringPrintf("--%s-wallpaper-small=%s",
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

  // Create the directory where policies for extensions installed in
  // device-local accounts are cached. This data is read and written by chronos.
  CHECK(EnsureDirectoryExists(
      base::FilePath("/var/cache/device_local_account_component_policy"),
      uid, gid, 0700));

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

  // Create the directory where the Quirks Client can store downloaded
  // icc and other display profiles.
  CHECK(EnsureDirectoryExists(
      base::FilePath("/var/cache/display_profiles"), uid, gid, 0700));

  // Create the directory for shared installed extensions.
  // Shared extensions are validated at runtime by the browser.
  // These extensions are read and written by chronos.
  CHECK(EnsureDirectoryExists(
      base::FilePath("/var/cache/shared_extensions"), uid, gid, 0700));

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
  if (base::PathExists(stateful_etc.Append("enable_chromium_coredumps")))
    builder->EnableCoreDumps();
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
void AddUiFlags(ChromiumCommandBuilder* builder) {
  const base::FilePath data_dir = GetDataDir(builder);

  // Force OOBE on test images that have requested it.
  if (base::PathExists(base::FilePath("/root/.test_repeat_oobe"))) {
    base::DeleteFile(data_dir.Append(".oobe_completed"), false);
    base::DeleteFile(data_dir.Append("Local State"), false);
  }


  if (builder->UseFlagIsSet("kevin")) {
    // TODO(jdufault): Remove this once quick unlock launches on all boards.
    builder->AddFeatureEnableOverride("QuickUnlockPin");
    // TODO(jdufault): Remove once a dedicated USE flag exists for this.
    builder->AddArg("--ash-enable-palette");

    builder->AddArg("--num-raster-threads=1");
    builder->AddArg("--enable-hardware-overlays=single-fullscreen");
  }
  if (builder->UseFlagIsSet("veyron_minnie"))
    builder->AddArg("--enable-hardware-overlays=single-fullscreen");

  // TODO(crbug.com/574923): Remove this when rialto is enrolled and using
  // standard kiosk mode.
  if (builder->UseFlagIsSet("rialto")) {
    builder->AddArg("--disable-demo-mode");
    builder->AddArg("--kiosk");
    builder->AddArg("--login-user=chronos");
    builder->AddArg("--ash-hide-notifications-for-factory");
    builder->AddArg("--allow-data-roaming-by-default");
    builder->AddArg("--load-and-launch-app=/usr/share/app_shell/apps/rialto");
    builder->AddArg("--enable-logging=stderr");
    builder->AddArg("--log-level=0");
    builder->AddArg("about:blank");
  } else {
    builder->AddArg("--login-manager");
  }
  builder->AddArg("--login-profile=user");

  if (builder->UseFlagIsSet("natural_scroll_default"))
    builder->AddArg("--enable-natural-scroll-default");
  if (!builder->UseFlagIsSet("legacy_keyboard"))
    builder->AddArg("--has-chromeos-keyboard");
  if (builder->UseFlagIsSet("has_diamond_key"))
    builder->AddArg("--has-chromeos-diamond-key");

  if (builder->UseFlagIsSet("legacy_power_button"))
    builder->AddArg("--aura-legacy-power-button");

  if (builder->UseFlagIsSet("touchview")) {
    builder->AddArg("--enable-touchview");
    // TODO(jonross): Remove these flags once --enable-touchview controls
    // everything: http://crbug.com/521440
    builder->AddArg("--ash-enable-power-button-quick-lock");
    builder->AddArg("--enable-centered-app-list");
  }

  if (builder->UseFlagIsSet("disable_login_animations")) {
    builder->AddArg("--disable-login-animations");
    builder->AddArg("--disable-boot-animation");
    builder->AddArg("--ash-copy-host-background-at-boot");
  } else if (builder->UseFlagIsSet("fade_boot_splash_screen")) {
    builder->AddArg("--ash-animate-from-boot-splash-screen");
  }

  if (AddWallpaperFlags(builder, "default", "oem")) {
    builder->AddArg("--default-wallpaper-is-oem");
  } else {
    AddWallpaperFlags(builder, "default", "default");
    AddWallpaperFlags(builder, "child", "child");
  }
  AddWallpaperFlags(builder, "guest", "guest");

  // TODO(yongjaek): Remove the following flag when the kiosk mode app is ready
  // at crbug.com/309806.
  if (builder->UseFlagIsSet("moblab"))
    builder->AddArg("--disable-demo-mode");

  // Re-enable prefixed EME by default in Chrome OS.
  builder->AddArg("--enable-prefixed-encrypted-media");
}

// Adds enterprise-related flags to the command line.
void AddEnterpriseFlags(ChromiumCommandBuilder* builder) {
  builder->AddArg("--enterprise-enrollment-initial-modulus=15");
  builder->AddArg("--enterprise-enrollment-modulus-limit=19");

  // This flag is only used in M49 to enable bootstrapping for ChromeBit. All
  // Chrome OS devices will be eligible for bootstrapping starting from M50.
  // TODO(xdai): Remove this after it's cherry-picked in M49.
  if (builder->UseFlagIsSet("veyron_mickey"))
    builder->AddArg("--oobe-bootstrapping-slave");
}

// Adds patterns to the --vmodule flag.
void AddVmodulePatterns(ChromiumCommandBuilder* builder) {
  // There has been a steady supply of bug reports about screen locking. These
  // messages are useful for determining what happened within feedback reports.
  // See crbug.com/452599.
  builder->AddVmodulePattern("screen_locker=2");
  builder->AddVmodulePattern("webui_screen_locker=2");
  builder->AddVmodulePattern("lock_state_controller=2");
  builder->AddVmodulePattern("webui_login_view=2");
  builder->AddVmodulePattern("power_button_observer=2");

  // Turn on logging about external displays being connected and disconnected.
  // Different behavior is seen from different displays and these messages are
  // used to determine what happened within feedback reports.
  builder->AddVmodulePattern("*ui/display/chromeos*=1");
  builder->AddVmodulePattern("*ash/display*=1");

  // Turn on basic logging for Ozone platform implementations.
  builder->AddVmodulePattern("*ui/ozone*=1");

  // Turn on plugin loading failure logging for crbug.com/314301.
  builder->AddVmodulePattern("*zygote*=1");
  builder->AddVmodulePattern("*plugin*=2");

  // There is a mysterious offline login failure. Turn on logging on
  // the login code path.
  // TODO(xiyuan): Remove after http://crbug.com/547857 is resolved.
  builder->AddVmodulePattern("*chromeos/login/*=1");

  if (builder->UseFlagIsSet("cheets"))
    builder->AddVmodulePattern("*arc/*=1");
}

}  // namespace

void PerformChromeSetup(bool* is_developer_end_user_out,
                        std::map<std::string, std::string>* env_vars_out,
                        std::vector<std::string>* args_out,
                        uid_t* uid_out) {
  DCHECK(env_vars_out);
  DCHECK(args_out);
  DCHECK(uid_out);

  ChromiumCommandBuilder builder;
  CHECK(builder.Init());

  // Start X in the background before doing more-expensive setup.
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

  // Please add new code to the most-appropriate helper function instead of
  // putting it here. Things that to all Chromium-derived binaries (e.g.
  // app_shell, content_shell, etc.) rather than just to Chrome belong in the
  // ChromiumCommandBuilder class instead.
  CreateDirectories(&builder);
  InitCrashHandling(&builder);
  AddSystemFlags(&builder);
  AddUiFlags(&builder);
  AddEnterpriseFlags(&builder);
  AddVmodulePatterns(&builder);

  // Apply any modifications requested by the developer.
  if (builder.is_developer_end_user())
    builder.ApplyUserConfig(base::FilePath(kChromeDevConfigPath));

  *is_developer_end_user_out = builder.is_developer_end_user();
  *env_vars_out = builder.environment_variables();
  *args_out = builder.arguments();
  *uid_out = builder.uid();

  if (using_x11)
    CHECK(x_runner->WaitForServer());

  // Do not add code here. Potentially-expensive work should be done between
  // StartServer() and WaitForServer().
}

}  // namespace login_manager
