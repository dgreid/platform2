// Copyright (c) 2010 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "metrics/metrics_library.h"

#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/guid.h>
#include <base/logging.h>
#include <base/stl_util.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <errno.h>
#include <session_manager/dbus-proxies.h>
#include <sys/file.h>
#include <sys/stat.h>

#include <cstdio>
#include <cstring>
#include <vector>

#include "metrics/serialization/metric_sample.h"
#include "metrics/serialization/serialization_utils.h"

#include "policy/device_policy.h"

using org::chromium::SessionManagerInterfaceProxy;

namespace {

const char kUMAEventsPath[] = "/var/lib/metrics/uma-events";
const char kConsentFile[] = "/home/chronos/Consent To Send Stats";
const char kCrosEventHistogramName[] = "Platform.CrOSEvent";
const int kCrosEventHistogramMax = 100;

// Add new cros events here.
//
// The index of the event is sent in the message, so please do not
// reorder the names.
//
// Note: All updates here must also update Chrome's enums.xml database.
// Please see this document for more details:
// https://chromium.googlesource.com/chromium/src/+/master/tools/metrics/histograms/
//
// You can view them live here:
// https://uma.googleplex.com/histograms/?histograms=Platform.CrOSEvent
const char* kCrosEventNames[] = {
    "ModemManagerCommandSendFailure",           // 0
    "HwWatchdogReboot",                         // 1
    "Cras.NoCodecsFoundAtBoot",                 // 2
    "Chaps.DatabaseCorrupted",                  // 3
    "Chaps.DatabaseRepairFailure",              // 4
    "Chaps.DatabaseCreateFailure",              // 5
    "Attestation.OriginSpecificExhausted",      // 6
    "SpringPowerSupply.Original.High",          // 7
    "SpringPowerSupply.Other.High",             // 8
    "SpringPowerSupply.Original.Low",           // 9
    "SpringPowerSupply.ChargerIdle",            // 10
    "TPM.NonZeroDictionaryAttackCounter",       // 11
    "TPM.EarlyResetDuringCommand",              // 12
    "VeyronEmmcUpgrade.Success",                // 13
    "VeyronEmmcUpgrade.WaitForKernelRollup",    // 14
    "VeyronEmmcUpgrade.WaitForFirmwareRollup",  // 15
    "VeyronEmmcUpgrade.BadEmmcProperties",      // 16
    "VeyronEmmcUpgrade.FailedDiskAccess",       // 17
    "VeyronEmmcUpgrade.FailedWPEnable",         // 18
    "VeyronEmmcUpgrade.SignatureDetected",      // 19
    "Watchdog.StartupFailed",                   // 20
    "Vm.VmcStart",                              // 21
    "Vm.VmcStartSuccess",                       // 22
    "Vm.DiskEraseFailed",                       // 23
    "Fingerprint.MCU.Reboot",                   // 24
    "Crash.Chrome.CrashesFromKernel",           // 25
    "Crash.Chrome.MissedCrashes",               // 26
    "Crash.Collector.CollectionCount",          // 27
    "Cryptohome.DoubleMountRequest",            // 28
    "SessionManager.SafeModeEnabled",           // 29
    "Crash.Sender.FailedCrashRemoval",          // 30
    "Crash.Sender.AttemptedCrashRemoval",       // 31
};

// Update this to be last entry + 1 when you add new entries to the end. Checks
// that no one tries to remove entries from the middle or misnumbers during a
// merge conflict.
static_assert(base::size(kCrosEventNames) == 32,
              "CrosEvent enums not lining up properly");

}  // namespace

time_t MetricsLibrary::cached_enabled_time_ = 0;
bool MetricsLibrary::cached_enabled_ = false;

MetricsLibrary::MetricsLibrary()
    : uma_events_file_(base::FilePath(kUMAEventsPath)),
      consent_file_(base::FilePath(kConsentFile)) {}

MetricsLibrary::~MetricsLibrary() {}

bool MetricsLibrary::IsGuestMode() {
  // Shortcut check whether there is any logged-in user.
  if (access("/run/state/logged-in", F_OK) != 0)
    return false;

  dbus::Bus::Options options;
  options.bus_type = dbus::Bus::SYSTEM;
  scoped_refptr<dbus::Bus> bus = new dbus::Bus(options);
  CHECK(bus->Connect());

  brillo::ErrorPtr error;
  bool is_guest = false;
  SessionManagerInterfaceProxy session_manager_interface(bus);
  session_manager_interface.IsGuestSessionActive(&is_guest, &error);
  return is_guest;
}

bool MetricsLibrary::ConsentId(std::string* id) {
  // Do not allow symlinks.
  base::ScopedFD fd(
      open(consent_file_.value().c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
  if (fd.get() < 0)
    return false;

  // We declare a slightly larger buffer than needed so we can detect if it's
  // been corrupted with a lot of bad data.
  char buf[40];
  ssize_t len = read(fd.get(), buf, sizeof(buf));

  // If we couldn't get any data, just fail right away.
  if (len <= 0)
    return false;

  // Chop the trailing newline to make parsing below easier.
  if (buf[len - 1] == '\n')
    buf[--len] = '\0';

  // Make sure it's a valid UUID.  Support older installs that omitted dashes.
  if (len != 32 && len != 36)
    return false;

  ssize_t i;
  id->clear();
  for (i = 0; i < len; ++i) {
    char c = buf[i];
    *id += c;

    // For long UUIDs, require dashes at certain positions.
    if (len == 36 && (i == 8 || i == 13 || i == 18 || i == 23)) {
      if (c == '-')
        continue;
      return false;
    }

    // All the rest should be hexdigits.
    if (base::IsHexDigit(c))
      continue;

    return false;
  }

  return true;
}

bool MetricsLibrary::AreMetricsEnabled() {
  time_t this_check_time = time(nullptr);
  if (this_check_time != cached_enabled_time_) {
    cached_enabled_time_ = this_check_time;

    if (!policy_provider_.get())
      policy_provider_.reset(new policy::PolicyProvider());
    policy_provider_->Reload();

    const policy::DevicePolicy* device_policy = nullptr;
    if (policy_provider_->device_policy_is_loaded())
      device_policy = &policy_provider_->GetDevicePolicy();

    // If policy couldn't be loaded or the metrics policy is not set, default to
    // enabled for enterprise-enrolled devices, cf. https://crbug/456186, or
    // respect the consent file if it is present for migration purposes. In all
    // other cases, default to disabled.
    // TODO(pastarmovj)
    std::string id_unused;
    bool metrics_enabled = false;
    bool metrics_policy = false;
    if (device_policy && device_policy->GetMetricsEnabled(&metrics_policy)) {
      metrics_enabled = metrics_policy;
      VLOG(2) << "AreMetricsEnabled: " << metrics_enabled << " (device policy)";
    } else if (device_policy && device_policy->IsEnterpriseManaged()) {
      metrics_enabled = true;
      VLOG(2) << "AreMetricsEnabled: 1 (enterprise managed)";
    } else {
      metrics_enabled = ConsentId(&id_unused);
      VLOG(2) << "AreMetricsEnabled: " << metrics_enabled
              << "(consent ID file)";
    }
    cached_enabled_ = (metrics_enabled && !IsGuestMode());
  }
  return cached_enabled_;
}

bool MetricsLibrary::EnableMetrics() {
  // Already enabled? Don't touch anything.
  if (AreMetricsEnabled())
    return true;

  std::string guid = base::GenerateGUID();

  if (guid.empty())
    return false;

  // http://crbug.com/383003 says we must be world readable.
  mode_t mask = umask(0022);
  int write_len = base::WriteFile(base::FilePath(consent_file_), guid.c_str(),
                                  guid.length());
  umask(mask);

  return write_len == static_cast<int>(guid.length());
}

bool MetricsLibrary::DisableMetrics() {
  return base::DeleteFile(base::FilePath(consent_file_), false);
}

void MetricsLibrary::Init() {
  // Deprecated.  Initialization code should go in constructor.
  // Remove this function when it is no longer used.
}

void MetricsLibrary::SetOutputFile(const std::string& output_file) {
  uma_events_file_ = base::FilePath(output_file);
}

bool MetricsLibrary::Replay(const std::string& input_file) {
  std::vector<metrics::MetricSample> samples;
  if (!metrics::SerializationUtils::ReadAndTruncateMetricsFromFile(
          input_file, &samples,
          metrics::SerializationUtils::kSampleBatchMaxLength)) {
    return false;
  }
  return metrics::SerializationUtils::WriteMetricsToFile(
      samples, uma_events_file_.value());
}

bool MetricsLibrary::SendToUMA(
    const std::string& name, int sample, int min, int max, int nbuckets) {
  return metrics::SerializationUtils::WriteMetricsToFile(
      {metrics::MetricSample::HistogramSample(name, sample, min, max,
                                              nbuckets)},
      uma_events_file_.value());
}

#if USE_METRICS_UPLOADER
bool MetricsLibrary::SendRepeatedToUMA(const std::string& name,
                                       int sample,
                                       int min,
                                       int max,
                                       int nbuckets,
                                       int num_samples) {
  return metrics::SerializationUtils::WriteMetricsToFile(
      {metrics::MetricSample::HistogramSample(name, sample, min, max, nbuckets,
                                              num_samples)},
      uma_events_file_.value());
}
#endif

void MetricsLibrary::SetConsentFileForTest(const base::FilePath& consent_file) {
  consent_file_ = consent_file;
}

bool MetricsLibrary::SendEnumToUMA(const std::string& name,
                                   int sample,
                                   int max) {
  return metrics::SerializationUtils::WriteMetricsToFile(
      {metrics::MetricSample::LinearHistogramSample(name, sample, max)},
      uma_events_file_.value());
}

bool MetricsLibrary::SendBoolToUMA(const std::string& name, bool sample) {
  return metrics::SerializationUtils::WriteMetricsToFile(
      {metrics::MetricSample::LinearHistogramSample(name, sample ? 1 : 0, 2)},
      uma_events_file_.value());
}

bool MetricsLibrary::SendSparseToUMA(const std::string& name, int sample) {
  return metrics::SerializationUtils::WriteMetricsToFile(
      {metrics::MetricSample::SparseHistogramSample(name, sample)},
      uma_events_file_.value());
}

bool MetricsLibrary::SendUserActionToUMA(const std::string& action) {
  return metrics::SerializationUtils::WriteMetricsToFile(
      {metrics::MetricSample::UserActionSample(action)},
      uma_events_file_.value());
}

bool MetricsLibrary::SendCrashToUMA(const char* crash_kind) {
  return metrics::SerializationUtils::WriteMetricsToFile(
      {metrics::MetricSample::CrashSample(crash_kind)},
      uma_events_file_.value());
}

void MetricsLibrary::SetPolicyProvider(policy::PolicyProvider* provider) {
  policy_provider_.reset(provider);
}

bool MetricsLibrary::SendCrosEventToUMA(const std::string& event) {
  for (size_t i = 0; i < base::size(kCrosEventNames); i++) {
    if (strcmp(event.c_str(), kCrosEventNames[i]) == 0) {
      return SendEnumToUMA(kCrosEventHistogramName, i, kCrosEventHistogramMax);
    }
  }
  LOG(WARNING) << "Unknown CrosEvent '" << event << "'";
  return false;
}
