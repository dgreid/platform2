// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "debugd/src/debugd_dbus_adaptor.h"

#include <memory>
#include <utility>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/memory/ref_counted.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <brillo/variant_dictionary.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/object_path.h>

#include "debugd/src/constants.h"
#include "debugd/src/error_utils.h"
#include "debugd/src/process_with_output.h"

namespace debugd {

namespace {

const char kDevCoredumpDBusErrorString[] =
    "org.chromium.debugd.error.DevCoreDump";

const char kShouldSendRlzPingKey[] = "should_send_rlz_ping";

const char kRlzEmbargoEndDateKey[] = "rlz_embargo_end_date";

}  // namespace

DebugdDBusAdaptor::DebugdDBusAdaptor(scoped_refptr<dbus::Bus> bus)
    : org::chromium::debugdAdaptor(this),
      dbus_object_(nullptr, bus, dbus::ObjectPath(kDebugdServicePath)) {
  battery_tool_ = std::make_unique<BatteryTool>();
  container_tool_ = std::make_unique<ContainerTool>();
  crash_sender_tool_ = std::make_unique<CrashSenderTool>();
  cups_tool_ = std::make_unique<CupsTool>();
  cros_healthd_tool_ = std::make_unique<CrosHealthdTool>();
  debug_logs_tool_ = std::make_unique<DebugLogsTool>(bus);
  debug_mode_tool_ = std::make_unique<DebugModeTool>(bus);
  dev_features_tool_wrapper_ =
      std::make_unique<RestrictedToolWrapper<DevFeaturesTool>>(bus);
  example_tool_ = std::make_unique<ExampleTool>();
  icmp_tool_ = std::make_unique<ICMPTool>();
  ipaddrs_tool_ = std::make_unique<IpAddrsTool>();
  log_tool_ = std::make_unique<LogTool>(bus);
  memory_tool_ = std::make_unique<MemtesterTool>();
  netif_tool_ = std::make_unique<NetifTool>();
  network_status_tool_ = std::make_unique<NetworkStatusTool>();
  oom_adj_tool_ = std::make_unique<OomAdjTool>();
  packet_capture_tool_ = std::make_unique<PacketCaptureTool>();
  perf_tool_ = std::make_unique<PerfTool>();
  ping_tool_ = std::make_unique<PingTool>();
  probe_tool_ = std::make_unique<ProbeTool>();
  route_tool_ = std::make_unique<RouteTool>();
  shill_scripts_tool_ = std::make_unique<ShillScriptsTool>();
  storage_tool_ = std::make_unique<StorageTool>();
  swap_tool_ = std::make_unique<SwapTool>();
  sysrq_tool_ = std::make_unique<SysrqTool>();
  systrace_tool_ = std::make_unique<SystraceTool>();
  tracepath_tool_ = std::make_unique<TracePathTool>();
  u2f_tool_ = std::make_unique<U2fTool>();
  verify_ro_tool_ = std::make_unique<VerifyRoTool>();
  vm_concierge_tool_ = std::make_unique<SimpleServiceTool>(
      "vm_concierge", bus, vm_tools::concierge::kVmConciergeServiceName,
      vm_tools::concierge::kVmConciergeServicePath);
  vm_plugin_dispatcher_tool_ = std::make_unique<SimpleServiceTool>(
      "vmplugin_dispatcher", bus,
      vm_tools::plugin_dispatcher::kVmPluginDispatcherServiceName,
      vm_tools::plugin_dispatcher::kVmPluginDispatcherServicePath);
  wifi_fw_dump_tool_ = std::make_unique<WifiFWDumpTool>();
  wifi_power_tool_ = std::make_unique<WifiPowerTool>();
  session_manager_proxy_ = std::make_unique<SessionManagerProxy>(bus);
  scheduler_configuration_tool_ =
      std::make_unique<SchedulerConfigurationTool>();
  if (dev_features_tool_wrapper_->restriction().InDevMode() &&
      base::PathExists(
          base::FilePath(debugd::kDevFeaturesChromeRemoteDebuggingFlagPath))) {
    session_manager_proxy_->EnableChromeRemoteDebugging();
  }
}

void DebugdDBusAdaptor::RegisterAsync(
    const brillo::dbus_utils::AsyncEventSequencer::CompletionAction& cb) {
  auto* my_interface = dbus_object_.AddOrGetInterface(kDebugdInterface);
  DCHECK(my_interface);
  my_interface->AddProperty(kCrashSenderTestMode, &crash_sender_test_mode_);
  crash_sender_test_mode_.SetUpdateCallback(
      base::Bind(&CrashSenderTool::OnTestModeChanged,
                 base::Unretained(crash_sender_tool_.get())));
  crash_sender_test_mode_.SetValue(false);
  crash_sender_test_mode_.SetAccessMode(
      brillo::dbus_utils::ExportedPropertyBase::Access::kReadWrite);
  RegisterWithDBusObject(&dbus_object_);
  dbus_object_.RegisterAsync(cb);
}

std::string DebugdDBusAdaptor::SetOomScoreAdj(
    const std::map<pid_t, int32_t>& scores) {
  return oom_adj_tool_->Set(scores);
}

bool DebugdDBusAdaptor::PingStart(brillo::ErrorPtr* error,
                                  const base::ScopedFD& outfd,
                                  const std::string& destination,
                                  const brillo::VariantDictionary& options,
                                  std::string* handle) {
  return ping_tool_->Start(outfd, destination, options, handle, error);
}

bool DebugdDBusAdaptor::PingStop(brillo::ErrorPtr* error,
                                 const std::string& handle) {
  return ping_tool_->Stop(handle, error);
}

std::string DebugdDBusAdaptor::TracePathStart(
    const base::ScopedFD& outfd,
    const std::string& destination,
    const brillo::VariantDictionary& options) {
  return tracepath_tool_->Start(outfd, destination, options);
}

bool DebugdDBusAdaptor::TracePathStop(brillo::ErrorPtr* error,
                                      const std::string& handle) {
  return tracepath_tool_->Stop(handle, error);
}

void DebugdDBusAdaptor::SystraceStart(const std::string& categories) {
  (void)systrace_tool_->Start(categories);
}

void DebugdDBusAdaptor::SystraceStop(const base::ScopedFD& outfd) {
  systrace_tool_->Stop(outfd);
}

std::string DebugdDBusAdaptor::SystraceStatus() {
  return systrace_tool_->Status();
}

std::vector<std::string> DebugdDBusAdaptor::GetIpAddresses(
    const brillo::VariantDictionary& options) {
  return ipaddrs_tool_->GetIpAddresses(options);
}

std::vector<std::string> DebugdDBusAdaptor::GetRoutes(
    const brillo::VariantDictionary& options) {
  return route_tool_->GetRoutes(options);
}

std::string DebugdDBusAdaptor::GetNetworkStatus() {
  return network_status_tool_->GetNetworkStatus();
}

bool DebugdDBusAdaptor::GetPerfOutput(brillo::ErrorPtr* error,
                                      uint32_t duration_sec,
                                      const std::vector<std::string>& perf_args,
                                      int32_t* status,
                                      std::vector<uint8_t>* perf_data,
                                      std::vector<uint8_t>* perf_stat) {
  return perf_tool_->GetPerfOutput(duration_sec, perf_args, perf_data,
                                   perf_stat, status, error);
}

bool DebugdDBusAdaptor::GetPerfOutputFd(
    brillo::ErrorPtr* error,
    uint32_t duration_sec,
    const std::vector<std::string>& perf_args,
    const base::ScopedFD& stdout_fd,
    uint64_t* session_id) {
  return perf_tool_->GetPerfOutputFd(duration_sec, perf_args, stdout_fd,
                                     session_id, error);
}

bool DebugdDBusAdaptor::StopPerf(brillo::ErrorPtr* error, uint64_t session_id) {
  return perf_tool_->StopPerf(session_id, error);
}

void DebugdDBusAdaptor::DumpDebugLogs(bool is_compressed,
                                      const base::ScopedFD& fd) {
  debug_logs_tool_->GetDebugLogs(is_compressed, fd);
}

void DebugdDBusAdaptor::SetDebugMode(const std::string& subsystem) {
  debug_mode_tool_->SetDebugMode(subsystem);
}

std::string DebugdDBusAdaptor::GetLog(const std::string& name) {
  return log_tool_->GetLog(name);
}

std::map<std::string, std::string> DebugdDBusAdaptor::GetAllLogs() {
  return log_tool_->GetAllLogs();
}

void DebugdDBusAdaptor::GetBigFeedbackLogs(const base::ScopedFD& fd,
                                           const std::string& username) {
  log_tool_->GetBigFeedbackLogs(fd, username);
}

void DebugdDBusAdaptor::BackupArcBugReport(const std::string& userhash) {
  log_tool_->BackupArcBugReport(userhash);
}

void DebugdDBusAdaptor::DeleteArcBugReportBackup(const std::string& userhash) {
  log_tool_->DeleteArcBugReportBackup(userhash);
}

void DebugdDBusAdaptor::GetJournalLog(const base::ScopedFD& fd) {
  log_tool_->GetJournalLog(fd);
}

std::string DebugdDBusAdaptor::GetExample() {
  return example_tool_->GetExample();
}

int32_t DebugdDBusAdaptor::CupsAddAutoConfiguredPrinter(
    const std::string& name, const std::string& uri) {
  return cups_tool_->AddAutoConfiguredPrinter(name, uri);
}

int32_t DebugdDBusAdaptor::CupsAddManuallyConfiguredPrinter(
    const std::string& name,
    const std::string& uri,
    const std::vector<uint8_t>& ppd_contents) {
  return cups_tool_->AddManuallyConfiguredPrinter(name, uri, ppd_contents);
}

bool DebugdDBusAdaptor::CupsRemovePrinter(const std::string& name) {
  return cups_tool_->RemovePrinter(name);
}

std::string DebugdDBusAdaptor::GetInterfaces() {
  return netif_tool_->GetInterfaces();
}

std::string DebugdDBusAdaptor::TestICMP(const std::string& host) {
  return icmp_tool_->TestICMP(host);
}

std::string DebugdDBusAdaptor::TestICMPWithOptions(
    const std::string& host,
    const std::map<std::string, std::string>& options) {
  return icmp_tool_->TestICMPWithOptions(host, options);
}

std::string DebugdDBusAdaptor::BatteryFirmware(const std::string& option) {
  return battery_tool_->BatteryFirmware(option);
}

std::string DebugdDBusAdaptor::Smartctl(const std::string& option) {
  return storage_tool_->Smartctl(option);
}

std::string DebugdDBusAdaptor::Mmc(const std::string& option) {
  return storage_tool_->Mmc(option);
}

std::string DebugdDBusAdaptor::Nvme(const std::string& option) {
  return storage_tool_->Nvme(option);
}

std::string DebugdDBusAdaptor::NvmeLog(const uint32_t page_id,
                                       const uint32_t length,
                                       bool raw_binary) {
  return storage_tool_->NvmeLog(page_id, length, raw_binary);
}

std::string DebugdDBusAdaptor::MemtesterStart(const base::ScopedFD& outfd,
                                              uint32_t memory) {
  return memory_tool_->Start(outfd, memory);
}

bool DebugdDBusAdaptor::MemtesterStop(brillo::ErrorPtr* error,
                                      const std::string& handle) {
  return memory_tool_->Stop(handle, error);
}

std::string DebugdDBusAdaptor::BadblocksStart(const base::ScopedFD& outfd) {
  return storage_tool_->Start(outfd);
}

bool DebugdDBusAdaptor::BadblocksStop(brillo::ErrorPtr* error,
                                      const std::string& handle) {
  return storage_tool_->Stop(handle, error);
}

bool DebugdDBusAdaptor::PacketCaptureStart(
    brillo::ErrorPtr* error,
    const base::ScopedFD& statfd,
    const base::ScopedFD& outfd,
    const brillo::VariantDictionary& options,
    std::string* handle) {
  return packet_capture_tool_->Start(statfd, outfd, options, handle, error);
}

bool DebugdDBusAdaptor::PacketCaptureStop(brillo::ErrorPtr* error,
                                          const std::string& handle) {
  return packet_capture_tool_->Stop(handle, error);
}

bool DebugdDBusAdaptor::LogKernelTaskStates(brillo::ErrorPtr* error) {
  return sysrq_tool_->LogKernelTaskStates(error);
}

void DebugdDBusAdaptor::UploadCrashes() {
  crash_sender_tool_->UploadCrashes();
}

bool DebugdDBusAdaptor::UploadSingleCrash(
    brillo::ErrorPtr* error,
    const std::vector<std::tuple<std::string, base::ScopedFD>>& in_files) {
  return crash_sender_tool_->UploadSingleCrash(in_files, error);
}

bool DebugdDBusAdaptor::RemoveRootfsVerification(brillo::ErrorPtr* error) {
  auto tool = dev_features_tool_wrapper_->GetTool(error);
  return tool && tool->RemoveRootfsVerification(error);
}

bool DebugdDBusAdaptor::EnableBootFromUsb(brillo::ErrorPtr* error) {
  auto tool = dev_features_tool_wrapper_->GetTool(error);
  return tool && tool->EnableBootFromUsb(error);
}

bool DebugdDBusAdaptor::EnableChromeRemoteDebugging(brillo::ErrorPtr* error) {
  auto tool = dev_features_tool_wrapper_->GetTool(error);
  return tool && tool->EnableChromeRemoteDebugging(error);
}

bool DebugdDBusAdaptor::ConfigureSshServer(brillo::ErrorPtr* error) {
  auto tool = dev_features_tool_wrapper_->GetTool(error);
  return tool && tool->ConfigureSshServer(error);
}

bool DebugdDBusAdaptor::SetUserPassword(brillo::ErrorPtr* error,
                                        const std::string& username,
                                        const std::string& password) {
  auto tool = dev_features_tool_wrapper_->GetTool(error);
  return tool && tool->SetUserPassword(username, password, error);
}

bool DebugdDBusAdaptor::EnableChromeDevFeatures(
    brillo::ErrorPtr* error, const std::string& root_password) {
  auto tool = dev_features_tool_wrapper_->GetTool(error);
  return tool && tool->EnableChromeDevFeatures(root_password, error);
}

bool DebugdDBusAdaptor::QueryDevFeatures(brillo::ErrorPtr* error,
                                         int32_t* features) {
  // Special case: if access fails here, we return DEV_FEATURES_DISABLED rather
  // than a D-Bus error. However, we still want to return an error if we can
  // access the tool but the tool execution fails.
  auto tool = dev_features_tool_wrapper_->GetTool(nullptr);
  if (!tool) {
    *features = DEV_FEATURES_DISABLED;
    return true;
  }

  return tool && tool->QueryDevFeatures(features, error);
}

bool DebugdDBusAdaptor::EnableDevCoredumpUpload(brillo::ErrorPtr* error) {
  if (base::PathExists(base::FilePath(debugd::kDeviceCoredumpUploadFlagPath))) {
    VLOG(1) << "Device coredump upload already enabled";
    return true;
  }
  if (base::WriteFile(base::FilePath(debugd::kDeviceCoredumpUploadFlagPath), "",
                      0) < 0) {
    DEBUGD_ADD_ERROR(error, kDevCoredumpDBusErrorString,
                     "Failed to write flag file.");
    PLOG(ERROR) << "Failed to write flag file.";
    return false;
  }
  return true;
}

bool DebugdDBusAdaptor::DisableDevCoredumpUpload(brillo::ErrorPtr* error) {
  if (!base::PathExists(
          base::FilePath(debugd::kDeviceCoredumpUploadFlagPath))) {
    VLOG(1) << "Device coredump upload already disabled";
    return true;
  }
  if (!base::DeleteFile(base::FilePath(debugd::kDeviceCoredumpUploadFlagPath),
                        false)) {
    DEBUGD_ADD_ERROR(error, kDevCoredumpDBusErrorString,
                     "Failed to delete flag file.");
    PLOG(ERROR) << "Failed to delete flag file.";
    return false;
  }
  return true;
}

bool DebugdDBusAdaptor::KstaledSetRatio(brillo::ErrorPtr* error,
                                        uint8_t kstaled_ratio,
                                        bool* out_result) {
  *out_result = swap_tool_->KstaledSetRatio(error, kstaled_ratio);
  return *out_result;
}

std::string DebugdDBusAdaptor::SwapEnable(int32_t size, bool change_now) {
  return swap_tool_->SwapEnable(size, change_now);
}

std::string DebugdDBusAdaptor::SwapDisable(bool change_now) {
  return swap_tool_->SwapDisable(change_now);
}

std::string DebugdDBusAdaptor::SwapStartStop(bool on) {
  return swap_tool_->SwapStartStop(on);
}

std::string DebugdDBusAdaptor::SwapStatus() {
  return swap_tool_->SwapStatus();
}

std::string DebugdDBusAdaptor::SwapSetParameter(
    const std::string& parameter_name, int32_t parameter_value) {
  return swap_tool_->SwapSetParameter(parameter_name, parameter_value);
}

std::string DebugdDBusAdaptor::SetU2fFlags(const std::string& flags) {
  return u2f_tool_->SetFlags(flags);
}

std::string DebugdDBusAdaptor::GetU2fFlags() {
  return u2f_tool_->GetFlags();
}

void DebugdDBusAdaptor::ContainerStarted() {
  container_tool_->ContainerStarted();
}

void DebugdDBusAdaptor::ContainerStopped() {
  container_tool_->ContainerStopped();
}

std::string DebugdDBusAdaptor::WifiFWDump() {
  return wifi_fw_dump_tool_->WifiFWDump();
}

std::string DebugdDBusAdaptor::SetWifiPowerSave(bool enable) {
  return wifi_power_tool_->SetWifiPowerSave(enable);
}

std::string DebugdDBusAdaptor::GetWifiPowerSave() {
  return wifi_power_tool_->GetWifiPowerSave();
}

bool DebugdDBusAdaptor::RunShillScriptStart(
    brillo::ErrorPtr* error,
    const base::ScopedFD& outfd,
    const std::string& script,
    const std::vector<std::string>& script_args,
    std::string* handle) {
  return shill_scripts_tool_->Run(outfd, script, script_args, handle, error);
}

bool DebugdDBusAdaptor::RunShillScriptStop(brillo::ErrorPtr* error,
                                           const std::string& handle) {
  return shill_scripts_tool_->Stop(handle, error);
}

void DebugdDBusAdaptor::StartVmConcierge(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response) {
  vm_concierge_tool_->StartService({}, std::move(response));
}

void DebugdDBusAdaptor::StopVmConcierge() {
  vm_concierge_tool_->StopService();
}

void DebugdDBusAdaptor::StartVmPluginDispatcher(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response,
    const std::string& in_user_id_hash,
    const std::string& in_lang) {
  // Perform basic validation of user ID hash.
  if (in_user_id_hash.length() != 40) {
    LOG(ERROR) << "Incorrect length of the user_id_hash (" << in_user_id_hash
               << ")";
    response->Return(false);
    return;
  }

  if (!base::ContainsOnlyChars(in_user_id_hash, "abcdef0123456789")) {
    LOG(ERROR) << "user_id_hash should only contain lower case hex digits ("
               << in_user_id_hash << ")";
    response->Return(false);
    return;
  }

  // Perform basic validation of the language string. We expect it to be
  // <language>[-<territory>].
  std::vector<base::StringPiece> chunks = base::SplitStringPiece(
      in_lang, "-", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
  if (chunks.size() < 1 || chunks.size() > 2 || chunks[0].empty()) {
    LOG(ERROR) << "malformed language argument (" << in_lang << ")";
    response->Return(false);
    return;
  }

  vm_plugin_dispatcher_tool_->StartService(
      {{"CROS_USER_ID_HASH", in_user_id_hash}, {"CROS_USER_UI_LANG", in_lang}},
      std::move(response));
}

void DebugdDBusAdaptor::StopVmPluginDispatcher() {
  vm_plugin_dispatcher_tool_->StopService();
}

bool DebugdDBusAdaptor::SetRlzPingSent(brillo::ErrorPtr* error) {
  std::string stderr;
  int result = ProcessWithOutput::RunProcess(
      "/usr/sbin/vpd",
      {"-i", "RW_VPD", "-s", std::string(kShouldSendRlzPingKey) + "=0"},
      true,     // requires root
      false,    // disable_sandbox
      nullptr,  // stdin
      nullptr,  // stdout
      &stderr, error);
  if (result != EXIT_SUCCESS) {
    std::string error_string =
        "Failed to set vpd key: " + std::string(kShouldSendRlzPingKey) +
        " with exit code: " + std::to_string(result) + " with error: " + stderr;
    DEBUGD_ADD_ERROR(error, kDevCoredumpDBusErrorString, error_string);
    PLOG(ERROR) << error_string;
    return false;
  }
  // Remove |kRlzEmbargoEndDateKey|, which is no longer useful after
  // |kShouldSendRlzPingKey| is updated.
  result = ProcessWithOutput::RunProcess(
      "/usr/sbin/vpd",
      {"-i", "RW_VPD", "-d", std::string(kRlzEmbargoEndDateKey)},
      true,     // requires root
      false,    // disable_sandbox
      nullptr,  // stdin
      nullptr,  // stdout
      &stderr, error);
  if (result != EXIT_SUCCESS) {
    std::string error_string =
        "Failed to delete vpd key: " + std::string(kRlzEmbargoEndDateKey) +
        " with exit code: " + std::to_string(result) + " with error: " + stderr;
    DEBUGD_ADD_ERROR(error, kDevCoredumpDBusErrorString, error_string);
    PLOG(ERROR) << error_string;
  }
  // Regenerate the vpd cache log.
  result = ProcessWithOutput::RunProcess("/usr/sbin/dump_vpd_log", {"--force"},
                                         true,     // requires root
                                         false,    // disable_sandbox
                                         nullptr,  // stdin
                                         nullptr,  // stdout
                                         &stderr, error);
  if (result != EXIT_SUCCESS) {
    std::string error_string =
        "Failed to dump vpd log with exit code: " + std::to_string(result) +
        " with error: " + stderr;
    DEBUGD_ADD_ERROR(error, kDevCoredumpDBusErrorString, error_string);
    PLOG(ERROR) << error_string;
  }
  // The client only cares if updating |kShouldSendRlzPingKey| is successful, so
  // returns true regardless of the result of removing |kRlzEmbargoEndDateKey|
  // or the cache log update.
  return true;
}

bool DebugdDBusAdaptor::UpdateAndVerifyFWOnUsbStart(
    brillo::ErrorPtr* error,
    const base::ScopedFD& outfd,
    const std::string& image_file,
    const std::string& ro_db_dir,
    std::string* handle) {
  return verify_ro_tool_->UpdateAndVerifyFWOnUsb(error, outfd, image_file,
                                                 ro_db_dir, handle);
}

bool DebugdDBusAdaptor::UpdateAndVerifyFWOnUsbStop(brillo::ErrorPtr* error,
                                                   const std::string& handle) {
  return verify_ro_tool_->Stop(handle, error);
}

bool DebugdDBusAdaptor::SetSchedulerConfiguration(brillo::ErrorPtr* error,
                                                  const std::string& policy,
                                                  bool* result) {
  uint32_t num_cores_disabled;
  return SetSchedulerConfigurationV2(error, policy, false /* lock_policy */,
                                     result, &num_cores_disabled);
}

bool DebugdDBusAdaptor::SetSchedulerConfigurationV2(
    brillo::ErrorPtr* error,
    const std::string& policy,
    bool lock_policy,
    bool* result,
    uint32_t* num_cores_disabled) {
  *result = scheduler_configuration_tool_->SetPolicy(policy, lock_policy, error,
                                                     num_cores_disabled);
  return *result;
}

bool DebugdDBusAdaptor::EvaluateProbeFunction(
    brillo::ErrorPtr* error,
    const std::string& sandbox_info,
    const std::string& probe_statement,
    brillo::dbus_utils::FileDescriptor* outfd) {
  return probe_tool_->EvaluateProbeFunction(error, sandbox_info,
                                            probe_statement, outfd);
}

bool DebugdDBusAdaptor::CollectFanSpeed(brillo::ErrorPtr* error,
                                        std::string* output) {
  return cros_healthd_tool_->CollectFanSpeed(error, output);
}

bool DebugdDBusAdaptor::CollectSmartBatteryMetric(
    brillo::ErrorPtr* error,
    const std::string& metric_name,
    std::string* output) {
  return cros_healthd_tool_->CollectSmartBatteryMetric(error, metric_name,
                                                       output);
}

}  // namespace debugd
