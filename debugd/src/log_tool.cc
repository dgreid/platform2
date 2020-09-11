// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "debugd/src/log_tool.h"

#include <grp.h>
#include <inttypes.h>
#include <lzma.h>
#include <pwd.h>
#include <stdint.h>
#include <sys/types.h>
#include <unistd.h>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/base64.h>
#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/json/json_writer.h>
#include <base/logging.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <base/strings/utf_string_conversion_utils.h>
#include <base/values.h>

#include <chromeos/dbus/service_constants.h>
#include <shill/dbus-proxies.h>

#include "debugd/src/constants.h"
#include "debugd/src/perf_tool.h"
#include "debugd/src/process_with_output.h"

#include "brillo/key_value_store.h"
#include <brillo/osrelease_reader.h>
#include <brillo/cryptohome.h>

namespace debugd {

using std::string;

using Strings = std::vector<string>;

namespace {

const char kRoot[] = "root";
const char kShell[] = "/bin/sh";
constexpr char kLsbReleasePath[] = "/etc/lsb-release";
constexpr char kArcBugReportBackupFileName[] = "arc-bugreport.log";
constexpr char kArcBugReportBackupKey[] = "arc-bugreport-backup";
constexpr char kDaemonStoreBaseDir[] = "/run/daemon-store/debugd/";

// Minimum time in seconds needed to allow shill to test active connections.
const int kConnectionTesterTimeoutSeconds = 5;

// Default running perf for 2 seconds.
constexpr const int kPerfDurationSecs = 2;
// TODO(chinglinyu) Remove after crbug/934702 is fixed.
// The following description is added to 'perf-data' as a temporary solution
// before the update of feedback disclosure to users is done in crbug/934702.
constexpr const char kPerfDataDescription[] =
    "perf-data contains performance profiling information about how much time "
    "the system spends on various activities (program execution stack traces). "
    "This might reveal some information about what system features and "
    "resources are being used. The full detail of perf-data can be found in "
    "the PerfDataProto protocol buffer message type in the chromium source "
    "repository.\n";

#define CMD_KERNEL_MODULE_PARAMS(module_name) \
  "cd /sys/module/" #module_name "/parameters 2>/dev/null && grep -sH ^ *"

using Log = LogTool::Log;
constexpr Log::LogType kCommand = Log::kCommand;
constexpr Log::LogType kFile = Log::kFile;

class ArcBugReportLog : public LogTool::Log {
 public:
  ArcBugReportLog()
      : Log(kCommand,
            "arc-bugreport",
            "/usr/bin/nsenter -t1 -m /usr/sbin/android-sh -c "
            "/system/bin/arc-bugreport",
            kRoot,
            kRoot,
            10 * 1024 * 1024 /*10 MiB*/,
            LogTool::Encoding::kUtf8) {}

  virtual ~ArcBugReportLog() = default;
};

// NOTE: IF YOU ADD AN ENTRY TO THIS LIST, PLEASE:
// * add a row to http://go/cros-feedback-audit and fill it out
// * email cros-telemetry@
// (Eventually we'll have a better process, but for now please do this.)
// clang-format off
const std::vector<Log> kCommandLogs {
  // We need to enter init's mount namespace because it has /home/chronos
  // mounted which is where the consent knob lives.  We don't have that mount
  // in our own mount namespace (by design).  https://crbug.com/884249
  {kCommand, "CLIENT_ID", "/usr/bin/nsenter -t1 -m /usr/bin/metrics_client -i",
    kRoot, kDebugfsGroup},
  {kCommand, "LOGDATE", "/bin/date"},
  // We need to enter init's mount namespace to access /home/root. Also, we use
  // neither ARC container's mount namespace (with android-sh) nor
  // /opt/google/containers/android/rootfs/android-data/ so that we can get
  // results even when the container is down.
  {kCommand, "android_app_storage", "/usr/bin/nsenter -t1 -m "
   "/bin/sh -c \"/usr/bin/du -h /home/root/*/android-data/data/\"",
   kRoot, kDebugfsGroup},
  {kFile, "atrus_logs", "/var/log/atrus.log"},
  {kFile, "authpolicy", "/var/log/authpolicy.log"},
  {kCommand, "bootstat_summary", "/usr/bin/bootstat_summary",
    SandboxedProcess::kDefaultUser, SandboxedProcess::kDefaultGroup,
    Log::kDefaultMaxBytes, LogTool::Encoding::kAutodetect, true},
  {kFile, "bio_crypto_init.LATEST",
    "/var/log/bio_crypto_init/bio_crypto_init.LATEST"},
  {kFile, "bio_crypto_init.PREVIOUS",
    "/var/log/bio_crypto_init/bio_crypto_init.PREVIOUS"},
  {kFile, "biod.LATEST", "/var/log/biod/biod.LATEST"},
  {kFile, "biod.PREVIOUS", "/var/log/biod/biod.PREVIOUS"},
  {kFile, "bio_fw_updater.LATEST", "/var/log/biod/bio_fw_updater.LATEST"},
  {kFile, "bio_fw_updater.PREVIOUS", "/var/log/biod/bio_fw_updater.PREVIOUS"},
  {kFile, "bios_info", "/var/log/bios_info.txt"},
  {kCommand, "bios_log", "cat /sys/firmware/log "
    "/proc/device-tree/chosen/ap-console-buffer 2>/dev/null"},
  {kFile, "bios_times", "/var/log/bios_times.txt"},
  // Slow or non-responsive block devices could cause this command to stall. Use
  // a timeout to prevent this command from blocking log fetching. This command
  // is expected to take O(100ms) in the normal case.
  {kCommand, "blkid", "timeout -s KILL 5s /sbin/blkid", kRoot, kRoot},
  {kFile, "buddyinfo", "/proc/buddyinfo"},
  {kCommand, "cbi_info", "/usr/share/userfeedback/scripts/cbi_info", kRoot,
    kRoot},
  {kFile, "cheets_log", "/var/log/arc.log"},
  {kFile, "clobber.log", "/var/log/clobber.log"},
  {kFile, "clobber-state.log", "/var/log/clobber-state.log"},
  {kCommand, "chromeos-pgmem", "/usr/bin/chromeos-pgmem", kRoot, kRoot},
  {kFile, "chrome_system_log", "/var/log/chrome/chrome"},
  {kFile, "chrome_system_log.PREVIOUS", "/var/log/chrome/chrome.PREVIOUS"},
  // There might be more than one record, so grab them all.
  // Plus, for <linux-3.19, it's named "console-ramoops", but for newer
  // versions, it's named "console-ramoops-#".
  {kCommand, "console-ramoops",
    "cat /sys/fs/pstore/console-ramoops* 2>/dev/null"},
  {kFile, "cpuinfo", "/proc/cpuinfo"},
  {kFile, "cr50_version", "/var/cache/cr50-version"},
  {kFile, "cros_ec.log", "/var/log/cros_ec.log",
    SandboxedProcess::kDefaultUser, SandboxedProcess::kDefaultGroup,
    Log::kDefaultMaxBytes, LogTool::Encoding::kUtf8},
  {kFile, "cros_ec.previous", "/var/log/cros_ec.previous",
    SandboxedProcess::kDefaultUser, SandboxedProcess::kDefaultGroup,
    Log::kDefaultMaxBytes, LogTool::Encoding::kUtf8},
  {kFile, "cros_ec_panicinfo", "/sys/kernel/debug/cros_ec/panicinfo",
    SandboxedProcess::kDefaultUser, kDebugfsGroup, Log::kDefaultMaxBytes,
    LogTool::Encoding::kBase64},
  {kCommand, "cros_ec_pdinfo",
    "for port in 0 1 2 3 4 5 6 7 8; do "
      "echo \"-----------\"; "
      // stderr output just tells us it failed
      "ectool usbpd \"${port}\" 2>/dev/null || break; "
    "done", kRoot, kRoot},
  {kFile, "cros_fp.previous", "/var/log/cros_fp.previous",
    SandboxedProcess::kDefaultUser, SandboxedProcess::kDefaultGroup,
    Log::kDefaultMaxBytes, LogTool::Encoding::kUtf8},
  {kFile, "cros_fp.log", "/var/log/cros_fp.log",
    SandboxedProcess::kDefaultUser, SandboxedProcess::kDefaultGroup,
    Log::kDefaultMaxBytes, LogTool::Encoding::kUtf8},
  {kFile, "cros_ish.previous", "/var/log/cros_ish.previous",
    SandboxedProcess::kDefaultUser, SandboxedProcess::kDefaultGroup,
    Log::kDefaultMaxBytes, LogTool::Encoding::kUtf8},
  {kFile, "cros_ish.log", "/var/log/cros_ish.log",
    SandboxedProcess::kDefaultUser, SandboxedProcess::kDefaultGroup,
    Log::kDefaultMaxBytes, LogTool::Encoding::kUtf8},
  {kCommand, "crosvm.log", "nsenter -t1 -m /bin/sh -c 'tail -n+1"
    " /run/daemon-store/crosvm/*/log/*.log.1"
    " /run/daemon-store/crosvm/*/log/*.log'", kRoot, kRoot},
  {kCommand, "dmesg", "/bin/dmesg"},
  {kCommand, "drm_gem_objects", "cat /sys/kernel/debug/dri/?/gem",
    SandboxedProcess::kDefaultUser, kDebugfsGroup},
  {kCommand, "drm_state", "cat /sys/kernel/debug/dri/?/state",
    SandboxedProcess::kDefaultUser, kDebugfsGroup},
  {kFile, "ec_info", "/var/log/ec_info.txt"},
  {kCommand, "edid-decode",
    "for f in /sys/class/drm/card?-*/edid; do "
      "echo \"----------- ${f}\"; "
      // edid-decode's stderr output is redundant, so silence it.
      "edid-decode \"${f}\" 2>/dev/null; "
    "done"},
  {kFile, "eventlog", "/var/log/eventlog.txt"},
  {kCommand, "font_info", "/usr/share/userfeedback/scripts/font_info"},
  {kCommand, "framebuffer", "cat /sys/kernel/debug/dri/?/framebuffer",
    SandboxedProcess::kDefaultUser, kDebugfsGroup},
  {kFile, "fwupd_state", "/var/lib/fwupd/state.json"},
  {kCommand, "sensor_info", "/usr/share/userfeedback/scripts/sensor_info"},
  {kFile, "hammerd", "/var/log/hammerd.log"},
  {kCommand, "hardware_class", "/usr/bin/crossystem hwid"},
  {kFile, "hardware_verification_report",
    "/var/cache/hardware_verifier.result"},
  {kCommand, "hostname", "/bin/hostname"},
  {kFile, "i915_gem_gtt", "/sys/kernel/debug/dri/0/i915_gem_gtt",
    SandboxedProcess::kDefaultUser, kDebugfsGroup},
  {kFile, "i915_gem_objects", "/sys/kernel/debug/dri/0/i915_gem_objects",
    SandboxedProcess::kDefaultUser, kDebugfsGroup},
  {kCommand, "i915_error_state",
    "/usr/bin/xz -c /sys/kernel/debug/dri/0/i915_error_state 2>/dev/null",
    SandboxedProcess::kDefaultUser, kDebugfsGroup, Log::kDefaultMaxBytes,
    LogTool::Encoding::kBase64},
  {kCommand, "ifconfig", "/bin/ifconfig -a"},
  {kFile, "input_devices", "/proc/bus/input/devices"},
  // Hardware capabilities of the wiphy device.
  {kFile, "interrupts", "/proc/interrupts"},
  {kCommand, "iw_list", "/usr/sbin/iw list"},
#if USE_IWLWIFI_DUMP
  {kCommand, "iwlmvm_module_params", CMD_KERNEL_MODULE_PARAMS(iwlmvm)},
  {kCommand, "iwlwifi_module_params", CMD_KERNEL_MODULE_PARAMS(iwlwifi)},
#endif  // USE_IWLWIFI_DUMP
  {kCommand, "kernel-crashes",
    "cat /var/spool/crash/kernel.*.kcrash 2>/dev/null"},
  {kCommand, "lsblk", "timeout -s KILL 5s lsblk -a", kRoot, kRoot,
    Log::kDefaultMaxBytes, LogTool::Encoding::kAutodetect, true},
  {kCommand, "lsmod", "lsmod"},
  {kCommand, "lspci", "/usr/sbin/lspci"},
  {kCommand, "lsusb", "lsusb && lsusb -t"},
  {kFile, "mali_memory", "/sys/kernel/debug/mali0/gpu_memory",
    SandboxedProcess::kDefaultUser, kDebugfsGroup},
  {kFile, "memd.parameters", "/var/log/memd/memd.parameters"},
  {kCommand, "memd clips", "cat /var/log/memd/memd.clip* 2>/dev/null"},
  {kFile, "meminfo", "/proc/meminfo"},
  {kCommand, "memory_spd_info",
    // mosys may use 'i2c-dev', which may not be loaded yet.
    "modprobe i2c-dev 2>/dev/null && mosys -l memory spd print all 2>/dev/null",
    kRoot, kDebugfsGroup},
  // The sed command finds the EDID blob (starting the line after "value:") and
  // replaces the serial number with all zeroes.
  //
  // The EDID is printed as a hex dump over several lines, each line containing
  // the contents of 16 bytes. The first 16 bytes are broken down as follows:
  //   uint64_t fixed_pattern;      // Always 00 FF FF FF FF FF FF 00.
  //   uint16_t manufacturer_id;    // Manufacturer ID, encoded as PNP IDs.
  //   uint16_t product_code;       // Manufacturer product code, little-endian.
  //   uint32_t serial_number;      // Serial number, little-endian.
  // Source: https://en.wikipedia.org/wiki/EDID#EDID_1.3_data_format
  //
  // The subsequent substitution command looks for the fixed pattern followed by
  // two 32-bit fields (manufacturer + product, serial number). It replaces the
  // latter field with 8 bytes of zeroes.
  //
  // TODO(crbug.com/731133): Remove the sed command once modetest itself can
  // remove serial numbers.
  {kCommand, "modetest",
    "(modetest; modetest -M evdi; modetest -M udl) | "
    "sed -E '/EDID/ {:a;n;/value:/!ba;n;"
    "s/(00f{12}00)([0-9a-f]{8})([0-9a-f]{8})/\\1\\200000000/}'",
    kRoot, kRoot},
  {kFile, "mount-encrypted", "/var/log/mount-encrypted.log"},
  {kFile, "mountinfo", "/proc/1/mountinfo"},
  {kCommand, "netlog",
    "/usr/share/userfeedback/scripts/getmsgs /var/log/net.log"},
  {kFile, "nvmap_iovmm", "/sys/kernel/debug/nvmap/iovmm/allocations",
    SandboxedProcess::kDefaultUser, kDebugfsGroup},
  {kCommand, "oemdata", "/usr/share/cros/oemdata.sh", kRoot, kRoot},
  {kFile, "pagetypeinfo", "/proc/pagetypeinfo", kRoot},
  {kFile, "platform_identity_name",
    "/run/chromeos-config/v1/identity/platform-name"},
  {kFile, "platform_identity_model", "/run/chromeos-config/v1/name"},
  {kFile, "platform_identity_sku", "/run/chromeos-config/v1/identity/sku-id"},
  {kFile, "platform_identity_whitelabel_tag",
    "/run/chromeos-config/v1/identity/whitelabel-tag"},
  {kFile, "platform_identity_customization_id",
    "/run/chromeos-config/v1/identity/customization-id"},
  {kCommand, "power_supply_info", "/usr/bin/power_supply_info"},
  {kCommand, "power_supply_sysfs", "/usr/bin/print_sysfs_power_supply_data"},
  {kFile, "powerd.LATEST", "/var/log/power_manager/powerd.LATEST"},
  {kFile, "powerd.PREVIOUS", "/var/log/power_manager/powerd.PREVIOUS"},
  {kFile, "powerd.out", "/var/log/powerd.out"},
  {kFile, "powerwash_count", "/var/log/powerwash_count"},
  {kCommand, "ps", "/bin/ps auxZ"},
  // /proc/slabinfo is owned by root and has 0400 permission.
  {kFile, "slabinfo", "/proc/slabinfo", kRoot, kRoot},
  {kFile, "storage_info", "/var/log/storage_info.txt"},
  {kCommand, "swap_info", "/usr/share/cros/init/swap.sh status 2>/dev/null",
    SandboxedProcess::kDefaultUser, kDebugfsGroup},
  {kCommand, "syslog",
    "/usr/share/userfeedback/scripts/getmsgs /var/log/messages"},
  {kCommand, "system_log_stats",
    "echo 'BLOCK_SIZE=1024'; "
    "find /var/log/ -type f -exec du --block-size=1024 {} + | sort -n -r",
    kRoot, kRoot},
  {kCommand, "threads", "/bin/ps -T axo pid,ppid,spid,pcpu,ni,stat,time,comm"},
  {kFile, "tlsdate", "/var/log/tlsdate.log"},
  {kCommand, "top thread", "/usr/bin/top -Hbc -w128 -n 1 | head -n 40"},
  {kCommand, "top memory",
    "/usr/bin/top -o \"+%MEM\" -w128 -bcn 1 | head -n 57"},
  {kCommand, "touch_fw_version",
    "grep -aE"
    " -e 'synaptics: Touchpad model'"
    " -e 'chromeos-[a-z]*-touch-[a-z]*-update'"
    " /var/log/messages | tail -n 20"},
  {kCommand, "tpm-firmware-updater", "/usr/share/userfeedback/scripts/getmsgs "
    "/var/log/tpm-firmware-updater.log"},
  // TODO(jorgelo,mnissler): Don't run this as root.
  // On TPM 1.2 devices this will likely require adding a new user to the 'tss'
  // group.
  // On TPM 2.0 devices 'get_version_info' uses D-Bus and therefore can run as
  // any user.
  {kCommand, "tpm_version", "/usr/sbin/tpm-manager get_version_info", kRoot,
    kRoot},
  {kCommand, "atmel_ts_refs",
    "/opt/google/touch/scripts/atmel_tools.sh ts r", kRoot, kRoot},
  {kCommand, "atmel_tp_refs",
    "/opt/google/touch/scripts/atmel_tools.sh tp r", kRoot, kRoot},
  {kCommand, "atmel_ts_deltas",
    "/opt/google/touch/scripts/atmel_tools.sh ts d", kRoot, kRoot},
  {kCommand, "atmel_tp_deltas",
    "/opt/google/touch/scripts/atmel_tools.sh tp d", kRoot, kRoot},
  {kFile, "stateful_trim_state", "/var/lib/trim/stateful_trim_state"},
  {kFile, "stateful_trim_data", "/var/lib/trim/stateful_trim_data"},
  {kFile, "ui_log", "/var/log/ui/ui.LATEST"},
  {kCommand, "uname", "/bin/uname -a"},
  {kCommand, "update_engine.log",
    "cat $(ls -1tr /var/log/update_engine | tail -5 | sed"
    " s.^./var/log/update_engine/.)"},
  {kFile, "upstart", "/var/log/upstart.log"},
  {kCommand, "uptime", "/usr/bin/cut -d' ' -f1 /proc/uptime"},
  {kFile, "verified boot", "/var/log/debug_vboot_noisy.log"},
  {kFile, "vmlog.1.LATEST", "/var/log/vmlog/vmlog.1.LATEST"},
  {kFile, "vmlog.1.PREVIOUS", "/var/log/vmlog/vmlog.1.PREVIOUS"},
  {kFile, "vmlog.LATEST", "/var/log/vmlog/vmlog.LATEST"},
  {kFile, "vmlog.PREVIOUS", "/var/log/vmlog/vmlog.PREVIOUS"},
  {kFile, "vmstat", "/proc/vmstat"},
  {kFile, "vpd_2.0", "/var/log/vpd_2.0.txt"},
  {kFile, "zram compressed data size", "/sys/block/zram0/compr_data_size"},
  {kFile, "zram original data size", "/sys/block/zram0/orig_data_size"},
  {kFile, "zram total memory used", "/sys/block/zram0/mem_used_total"},
  {kFile, "zram total reads", "/sys/block/zram0/num_reads"},
  {kFile, "zram total writes", "/sys/block/zram0/num_writes"},
  {kCommand, "zram new stats names",
    "echo orig_size compr_size used_total limit used_max zero_pages migrated"},
  {kFile, "zram new stats values", "/sys/block/zram0/mm_stat"},
  {kFile, "cros_tp version", "/sys/class/chromeos/cros_tp/version"},
  {kCommand, "cros_tp console", "/usr/sbin/ectool --name=cros_tp console",
    kRoot, kRoot},
  {kCommand, "cros_tp frame", "/usr/sbin/ectool --name=cros_tp tpframeget",
    kRoot, kRoot},
  {kCommand, "crostini", "/usr/bin/cicerone_client --get_info"},
  // TODO(seanpaul): Once we've finished moving over to the upstream tracefs
  //                 implementation, remove drm_trace_legacy. Tracked in
  //                 b/163580546.
  {kFile, "drm_trace_legacy", "/sys/kernel/debug/dri/trace",
    SandboxedProcess::kDefaultUser, kDebugfsGroup},
  {kFile, "drm_trace", "/sys/kernel/debug/tracing/instances/drm/trace",
    SandboxedProcess::kDefaultUser, kDebugfsGroup},
  // Stuff pulled out of the original list. These need access to the running X
  // session, which we'd rather not give to debugd, or return info specific to
  // the current session (in the setsid(2) sense), which is not useful for
  // debugd
  // {kCommand, "env", "set"},
  // {kCommand, "setxkbmap", "/usr/bin/setxkbmap -print -query"},
  // {kCommand, "xrandr", "/usr/bin/xrandr --verbose}
};
// clang-format on

// Extra logs are logs such as netstat and logcat which should appear in
// chrome://system but not in feedback reports.  Open sockets may have privacy
// implications, and logcat is already incorporated via arc-bugreport.
//
// clang-format off
const std::vector<Log> kExtraLogs {
#if USE_CELLULAR
  {kCommand, "mm-status", "/usr/bin/modem status"},
#endif  // USE_CELLULAR
  {kCommand, "network-devices", "/usr/bin/connectivity show devices"},
  {kCommand, "network-services", "/usr/bin/connectivity show services"},
  {kCommand, "wifi_status_no_anonymize",
    "/usr/bin/network_diag --wifi-internal --no-log"},
  // --processes requires root.
  {kCommand, "netstat",
    "/sbin/ss --all --query inet --numeric --processes", kRoot, kRoot},
  {kCommand, "logcat",
    "/usr/bin/nsenter -t1 -m /usr/sbin/android-sh -c '/system/bin/logcat -d'",
    kRoot, kRoot, Log::kDefaultMaxBytes, LogTool::Encoding::kUtf8},
};
// clang-format on

// clang-format off
const std::vector<Log> kFeedbackLogs {
#if USE_CELLULAR
  {kCommand, "mm-status", "/usr/bin/modem status-feedback"},
#endif  // USE_CELLULAR
  {kCommand, "network-devices",
      "/usr/bin/connectivity show-feedback devices"},
  {kCommand, "network-services",
      "/usr/bin/connectivity show-feedback services"},
  {kCommand, "wifi_status",
      "/usr/bin/network_diag --wifi-internal --no-log --anonymize"},
};
// clang-format on

// Fills |dictionary| with the contents of the logs in |logs|.
void GetLogsInDictionary(const std::vector<Log>& logs,
                         base::DictionaryValue* dictionary) {
  for (const Log& log : logs) {
    dictionary->SetKey(log.GetName(), base::Value(log.GetLogData()));
  }
}

// Serializes the |dictionary| into the file with the given |fd| in a JSON
// format.
void SerializeLogsAsJSON(const base::DictionaryValue& dictionary,
                         const base::ScopedFD& fd) {
  string logs_json;
  base::JSONWriter::WriteWithOptions(
      dictionary, base::JSONWriter::OPTIONS_PRETTY_PRINT, &logs_json);
  base::WriteFileDescriptor(fd.get(), logs_json.c_str(), logs_json.size());
}

bool GetNamedLogFrom(const string& name,
                     const std::vector<Log>& logs,
                     string* result) {
  for (const Log& log : logs) {
    if (name == log.GetName()) {
      *result = log.GetLogData();
      return true;
    }
  }
  *result = "<invalid log name>";
  return false;
}

void GetLogsFrom(const std::vector<Log>& logs, LogTool::LogMap* map) {
  for (const Log& log : logs)
    (*map)[log.GetName()] = log.GetLogData();
}

void GetLsbReleaseInfo(LogTool::LogMap* map) {
  const base::FilePath lsb_release(kLsbReleasePath);
  brillo::KeyValueStore store;
  if (!store.Load(lsb_release)) {
    // /etc/lsb-release might not be present (cros deploying a new
    // configuration or no fields set at all). Just print a debug
    // message and continue.
    DLOG(INFO) << "Could not load fields from " << lsb_release.value();
  } else {
    for (const auto& key : store.GetKeys()) {
      std::string value;
      store.GetString(key, &value);
      (*map)[key] = value;
    }
  }
}

void GetOsReleaseInfo(LogTool::LogMap* map) {
  brillo::OsReleaseReader reader;
  reader.Load();
  for (const auto& key : reader.GetKeys()) {
    std::string value;
    reader.GetString(key, &value);
    (*map)["os-release " + key] = value;
  }
}

void PopulateDictionaryValue(const LogTool::LogMap& map,
                             base::DictionaryValue* dictionary) {
  for (const auto& kv : map) {
    dictionary->SetString(kv.first, kv.second);
  }
}

bool CompressXzBuffer(const std::vector<uint8_t>& in_buffer,
                      std::vector<uint8_t>* out_buffer) {
  size_t out_size = lzma_stream_buffer_bound(in_buffer.size());
  out_buffer->resize(out_size);
  size_t out_pos = 0;

  lzma_ret ret = lzma_easy_buffer_encode(
      LZMA_PRESET_DEFAULT, LZMA_CHECK_CRC64, nullptr, in_buffer.data(),
      in_buffer.size(), out_buffer->data(), &out_pos, out_size);

  if (ret != LZMA_OK) {
    out_buffer->clear();
    return false;
  }

  out_buffer->resize(out_pos);
  return true;
}

void GetPerfData(LogTool::LogMap* map) {
  // Run perf to collect system-wide performance profile when user triggers
  // feedback report. Perf runs at sampling frequency of ~500 hz (499 is used
  // to avoid sampling periodic system activities), with callstack in each
  // sample (-g).
  std::vector<std::string> perf_args = {
      "perf", "record", "-a", "-g", "-F", "499",
  };
  std::vector<uint8_t> perf_data;
  int32_t status;

  debugd::PerfTool perf_tool;
  if (!perf_tool.GetPerfOutput(kPerfDurationSecs, perf_args, &perf_data,
                               nullptr, &status, nullptr))
    return;

  // XZ compress the profile data.
  std::vector<uint8_t> perf_data_xz;
  if (!CompressXzBuffer(perf_data, &perf_data_xz))
    return;

  // Base64 encode the compressed data.
  std::string perf_data_str(reinterpret_cast<const char*>(perf_data_xz.data()),
                            perf_data_xz.size());
  (*map)["perf-data"] = std::string(kPerfDataDescription) +
                        LogTool::EncodeString(std::move(perf_data_str),
                                              LogTool::Encoding::kBase64);
}

}  // namespace

Log::Log(Log::LogType type,
         std::string name,
         std::string data,
         std::string user,
         std::string group,
         int64_t max_bytes,
         LogTool::Encoding encoding,
         bool access_root_mount_ns)
    : type_(type),
      name_(name),
      data_(data),
      user_(user),
      group_(group),
      max_bytes_(max_bytes),
      encoding_(encoding),
      access_root_mount_ns_(access_root_mount_ns) {}

std::string Log::GetName() const {
  return name_;
}

std::string Log::GetLogData() const {
  // The reason this code uses a switch statement on a type enum rather than
  // using inheritance/virtual dispatch is so that all of the Log objects can
  // be constructed statically. Switching to heap allocated subclasses of Log
  // makes the code that declares all of the log entries much more verbose
  // and harder to understand.
  std::string output;
  switch (type_) {
    case kCommand:
      output = GetCommandLogData();
      break;
    case kFile:
      output = GetFileLogData();
      break;
    default:
      return "<unknown log type>";
  }

  if (output.empty())
    return "<empty>";

  return LogTool::EncodeString(std::move(output), encoding_);
}

// TODO(ellyjones): sandbox. crosbug.com/35122
std::string Log::GetCommandLogData() const {
  if (type_ != kCommand)
    return "<log type mismatch>";
  std::string tailed_cmdline =
      base::StringPrintf("%s | tail -c %" PRId64, data_.c_str(), max_bytes_);
  ProcessWithOutput p;
  if (minijail_disabled_for_test_)
    p.set_use_minijail(false);
  if (!user_.empty() && !group_.empty())
    p.SandboxAs(user_, group_);
  if (access_root_mount_ns_)
    p.AllowAccessRootMountNamespace();
  if (!p.Init())
    return "<not available>";
  p.AddArg(kShell);
  p.AddStringOption("-c", tailed_cmdline);
  if (p.Run())
    return "<not available>";
  std::string output;
  p.GetOutput(&output);
  return output;
}

std::string Log::GetFileLogData() const {
  if (type_ != kFile)
    return "<log type mismatch>";

  uid_t old_euid = geteuid();
  uid_t new_euid = UidForUser(user_);
  gid_t old_egid = getegid();
  gid_t new_egid = GidForGroup(group_);

  if (new_euid == -1 || new_egid == -1) {
    return "<not available>";
  }

  // Make sure to set group first, since if we set user first we lose root
  // and therefore the ability to set our effective gid to arbitrary gids.
  if (setegid(new_egid)) {
    PLOG(ERROR) << "Failed to set effective group id to " << new_egid;
    return "<not available>";
  }
  if (seteuid(new_euid)) {
    PLOG(ERROR) << "Failed to set effective user id to " << new_euid;
    if (setegid(old_egid))
      PLOG(ERROR) << "Failed to restore effective group id to " << old_egid;
    return "<not available>";
  }

  std::string contents;
  const base::FilePath path(data_);
  // Handle special files that don't properly report length/allow lseek.
  if (base::FilePath("/dev").IsParent(path) ||
      base::FilePath("/proc").IsParent(path) ||
      base::FilePath("/sys").IsParent(path)) {
    if (!base::ReadFileToString(path, &contents))
      contents = "<not available>";
    if (contents.size() > max_bytes_)
      contents.erase(0, contents.size() - max_bytes_);
  } else {
    base::File file(path, base::File::FLAG_OPEN | base::File::FLAG_READ);
    if (!file.IsValid()) {
      contents = "<not available>";
    } else {
      int64_t length = file.GetLength();
      if (length > max_bytes_) {
        file.Seek(base::File::FROM_END, -max_bytes_);
        length = max_bytes_;
      }
      std::vector<char> buf(length);
      int read = file.ReadAtCurrentPos(buf.data(), buf.size());
      if (read < 0) {
        PLOG(ERROR) << "Could not read from file " << path.value();
      } else {
        contents = std::string(buf.begin(), buf.begin() + read);
      }
    }
  }

  // Make sure we restore our old euid/egid before returning.
  if (seteuid(old_euid))
    PLOG(ERROR) << "Failed to restore effective user id to " << old_euid;

  if (setegid(old_egid))
    PLOG(ERROR) << "Failed to restore effective group id to " << old_egid;

  return contents;
}

void Log::DisableMinijailForTest() {
  minijail_disabled_for_test_ = true;
}

// static
uid_t Log::UidForUser(const std::string& user) {
  struct passwd entry;
  struct passwd* result;
  std::vector<char> buf(1024);
  getpwnam_r(user.c_str(), &entry, &buf[0], buf.size(), &result);
  if (!result) {
    LOG(ERROR) << "User not found: " << user;
    return -1;
  }
  return entry.pw_uid;
}

// static
gid_t Log::GidForGroup(const std::string& group) {
  struct group entry;
  struct group* result;
  std::vector<char> buf(1024);
  getgrnam_r(group.c_str(), &entry, &buf[0], buf.size(), &result);
  if (!result) {
    LOG(ERROR) << "Group not found: " << group;
    return -1;
  }
  return entry.gr_gid;
}

LogTool::LogTool(
    scoped_refptr<dbus::Bus> bus,
    std::unique_ptr<org::chromium::CryptohomeInterfaceProxyInterface>
        cryptohome_proxy,
    std::unique_ptr<LogTool::Log> arc_bug_report_log,
    const base::FilePath& daemon_store_base_dir)
    : bus_(bus),
      cryptohome_proxy_(std::move(cryptohome_proxy)),
      arc_bug_report_log_(std::move(arc_bug_report_log)),
      daemon_store_base_dir_(daemon_store_base_dir) {}

LogTool::LogTool(scoped_refptr<dbus::Bus> bus)
    : LogTool(bus,
              std::make_unique<org::chromium::CryptohomeInterfaceProxy>(bus),
              std::make_unique<ArcBugReportLog>(),
              base::FilePath(kDaemonStoreBaseDir)) {}

base::FilePath LogTool::GetArcBugReportBackupFilePath(
    const std::string& userhash) {
  CHECK(brillo::cryptohome::home::IsSanitizedUserName(userhash))
      << "Invalid userhash '" << userhash << "'";

  return daemon_store_base_dir_.Append(userhash).Append(
      kArcBugReportBackupFileName);
}

void LogTool::CreateConnectivityReport(bool wait_for_results) {
  // Perform ConnectivityTrial to report connection state in feedback log.
  auto shill = std::make_unique<org::chromium::flimflam::ManagerProxy>(bus_);
  // Give the connection trial time to test the connection and log the results
  // before collecting the logs for feedback.
  // TODO(silberst): Replace the simple approach of a single timeout with a more
  // coordinated effort.
  if (shill && shill->CreateConnectivityReport(nullptr) && wait_for_results)
    sleep(kConnectionTesterTimeoutSeconds);
}

string LogTool::GetLog(const string& name) {
  string result;
  GetNamedLogFrom(name, kCommandLogs, &result) ||
      GetNamedLogFrom(name, kExtraLogs, &result) ||
      GetNamedLogFrom(name, kFeedbackLogs, &result);
  return result;
}

LogTool::LogMap LogTool::GetAllLogs() {
  CreateConnectivityReport(false);
  LogMap result;
  GetLogsFrom(kCommandLogs, &result);
  GetLogsFrom(kExtraLogs, &result);
  GetLsbReleaseInfo(&result);
  GetOsReleaseInfo(&result);
  return result;
}

LogTool::LogMap LogTool::GetAllDebugLogs() {
  CreateConnectivityReport(true);
  LogMap result;
  GetLogsFrom(kCommandLogs, &result);
  GetLogsFrom(kExtraLogs, &result);
  result[arc_bug_report_log_->GetName()] = GetArcBugReport("", nullptr);
  GetLsbReleaseInfo(&result);
  GetOsReleaseInfo(&result);
  return result;
}

void LogTool::GetBigFeedbackLogs(const base::ScopedFD& fd,
                                 const std::string& username) {
  CreateConnectivityReport(true);
  LogMap map;
  GetPerfData(&map);
  base::DictionaryValue dictionary;
  GetLogsInDictionary(kCommandLogs, &dictionary);
  GetLogsInDictionary(kFeedbackLogs, &dictionary);
  bool is_backup;
  std::string arc_bug_report = GetArcBugReport(username, &is_backup);
  dictionary.SetKey(kArcBugReportBackupKey,
                    base::Value(is_backup ? "true" : "false"));
  dictionary.SetKey(arc_bug_report_log_->GetName(),
                    base::Value(arc_bug_report));
  GetLsbReleaseInfo(&map);
  GetOsReleaseInfo(&map);
  PopulateDictionaryValue(map, &dictionary);
  SerializeLogsAsJSON(dictionary, fd);
}

std::string GetSanitizedUsername(
    org::chromium::CryptohomeInterfaceProxyInterface* cryptohome_proxy,
    const std::string& username) {
  if (username.empty()) {
    return std::string();
  }

  std::string sanitized_username;
  brillo::ErrorPtr error;
  if (!cryptohome_proxy->GetSanitizedUsername(username, &sanitized_username,
                                              &error)) {
    LOG(ERROR) << "Failed to call GetSanitizedUsername, error: "
               << error->GetMessage();
    return std::string();
  }

  return sanitized_username;
}

std::string LogTool::GetArcBugReport(const std::string& username,
                                     bool* is_backup) {
  if (is_backup) {
    *is_backup = true;
  }
  std::string userhash =
      GetSanitizedUsername(cryptohome_proxy_.get(), username);

  std::string contents;
  if (userhash.empty() ||
      arc_bug_report_backups_.find(userhash) == arc_bug_report_backups_.end() ||
      !base::ReadFileToString(GetArcBugReportBackupFilePath(userhash),
                              &contents)) {
    // If |userhash| was not empty, but was not found in the backup set
    // or the file did not exist, attempt to delete the file.
    if (!userhash.empty()) {
      DeleteArcBugReportBackup(username);
    }
    if (is_backup) {
      *is_backup = false;
    }
    contents = arc_bug_report_log_->GetLogData();
  }

  return contents;
}

void LogTool::BackupArcBugReport(const std::string& usernameOrUserhash) {
  DLOG(INFO) << "Backing up ARC bug report";

  const std::string userhash =
      brillo::cryptohome::home::IsSanitizedUserName(usernameOrUserhash)
          ? usernameOrUserhash
          : GetSanitizedUsername(cryptohome_proxy_.get(), usernameOrUserhash);

  const base::FilePath reportPath = GetArcBugReportBackupFilePath(userhash);
  const std::string logData = arc_bug_report_log_->GetLogData();
  if (base::WriteFile(reportPath, logData.c_str(), logData.length())) {
    arc_bug_report_backups_.insert(userhash);
  } else {
    PLOG(ERROR) << "Failed to backup ARC bug report";
  }
}

void LogTool::DeleteArcBugReportBackup(const std::string& usernameOrUserhash) {
  DLOG(INFO) << "Deleting the ARC bug report backup";

  const std::string userhash =
      brillo::cryptohome::home::IsSanitizedUserName(usernameOrUserhash)
          ? usernameOrUserhash
          : GetSanitizedUsername(cryptohome_proxy_.get(), usernameOrUserhash);

  const base::FilePath reportPath = GetArcBugReportBackupFilePath(userhash);
  arc_bug_report_backups_.erase(userhash);
  if (!base::DeleteFile(reportPath, false)) {
    PLOG(ERROR) << "Failed to delete ARC bug report backup";
  }
}

void LogTool::GetJournalLog(const base::ScopedFD& fd) {
  Log journal(kCommand, "journal.export", "journalctl -n 10000 -o export",
              "syslog", "syslog", 10 * 1024 * 1024, LogTool::Encoding::kBinary);
  std::string output = journal.GetLogData();
  base::WriteFileDescriptor(fd.get(), output.data(), output.size());
}

// static
string LogTool::EncodeString(string value, LogTool::Encoding source_encoding) {
  if (source_encoding == LogTool::Encoding::kBinary)
    return value;

  if (source_encoding == LogTool::Encoding::kAutodetect) {
    if (base::IsStringUTF8(value))
      return value;
    source_encoding = LogTool::Encoding::kBase64;
  }

  if (source_encoding == LogTool::Encoding::kUtf8) {
    string output;
    const char* src = value.data();
    int32_t src_len = static_cast<int32_t>(value.length());

    output.reserve(value.size());
    for (int32_t char_index = 0; char_index < src_len; char_index++) {
      uint32_t code_point;
      if (!base::ReadUnicodeCharacter(src, src_len, &char_index, &code_point) ||
          !base::IsValidCharacter(code_point)) {
        // Replace invalid characters with U+FFFD REPLACEMENT CHARACTER.
        code_point = 0xFFFD;
      }
      base::WriteUnicodeCharacter(code_point, &output);
    }
    return output;
  }

  base::Base64Encode(value, &value);
  return "<base64>: " + value;
}

}  // namespace debugd
