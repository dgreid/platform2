// Copyright (c) 2010 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef METRICS_METRICS_LIBRARY_H_
#define METRICS_METRICS_LIBRARY_H_

#include <sys/types.h>
#include <unistd.h>
#include <memory>
#include <string>

#include <base/compiler_specific.h>
#include <base/files/file_path.h>
#include <base/macros.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST

#include "policy/libpolicy.h"

class MetricsLibraryInterface {
 public:
  virtual void Init() = 0;  // TODO(chromium:940343): Remove this function.
  virtual bool AreMetricsEnabled() = 0;
  virtual bool IsGuestMode() = 0;
  virtual bool SendToUMA(
      const std::string& name, int sample, int min, int max, int nbuckets) = 0;
  virtual bool SendEnumToUMA(const std::string& name, int sample, int max) = 0;
  virtual bool SendBoolToUMA(const std::string& name, bool sample) = 0;
  virtual bool SendSparseToUMA(const std::string& name, int sample) = 0;
  virtual bool SendUserActionToUMA(const std::string& action) = 0;
  virtual bool SendCrashToUMA(const char* crash_kind) = 0;
  virtual bool SendCrosEventToUMA(const std::string& event) = 0;
#if USE_METRICS_UPLOADER
  virtual bool SendRepeatedToUMA(const std::string& name,
                                 int sample,
                                 int min,
                                 int max,
                                 int nbuckets,
                                 int num_samples) = 0;
#endif
  virtual ~MetricsLibraryInterface() {}
};

// Library used to send metrics to Chrome/UMA.
class MetricsLibrary : public MetricsLibraryInterface {
 public:
  MetricsLibrary();
  MetricsLibrary(const MetricsLibrary&) = delete;
  MetricsLibrary& operator=(const MetricsLibrary&) = delete;

  virtual ~MetricsLibrary();

  // Formerly used to initialize the library.
  // TODO(chromium:940343): Remove this function.
  void Init() override;

  // Returns whether or not the machine is running in guest mode.
  bool IsGuestMode() override;

  // Returns whether or not metrics collection is enabled.
  bool AreMetricsEnabled() override;

  // Chrome normally manages Enable/Disable state. These functions are
  // intended ONLY for use by devices which don't run Chrome (e.g. Onhub)
  // but are based on Chrome OS.
  // In those cases, "User Consent" is given via an "external" app
  // (e.g. cloud service or directly from a smart phone app).
  //
  // Enable metrics by creating and populating the Consent file.
  bool EnableMetrics();

  // Disable metrics by deleting the Consent file.
  bool DisableMetrics();

  // Look up the consent id for metrics reporting.
  // Note: Should only be used by internal system projects.
  bool ConsentId(std::string* id);

  // Send output to the specified file. This is
  // useful when running in a context where the metrics reporting system isn't
  // fully available (e.g. when /var is not mounted). Note that the contents of
  // custom output files will not be sent to the server automatically, but need
  // to be imported via Replay() to get picked up by the reporting pipeline.
  void SetOutputFile(const std::string& output_file);

  // Replays metrics from the given file as if the events contained in |file|
  // where being generated via the SendXYZ functions.
  bool Replay(const std::string& input_file);

  // Sends histogram data to Chrome for transport to UMA and returns
  // true on success. This method results in the equivalent of an
  // asynchronous non-blocking RPC to UMA_HISTOGRAM_CUSTOM_COUNTS
  // inside Chrome (see base/histogram.h).
  //
  // |sample| is the sample value to be recorded (|min| <= |sample| < |max|).
  // |min| is the minimum value of the histogram samples (|min| > 0).
  // |max| is the maximum value of the histogram samples.
  // |nbuckets| is the number of histogram buckets.
  // [0,min) is the implicit underflow bucket.
  // [|max|,infinity) is the implicit overflow bucket.
  //
  // Note that the memory allocated in Chrome for each histogram is
  // proportional to the number of buckets. Therefore, it is strongly
  // recommended to keep this number low (e.g., 50 is normal, while
  // 100 is high).
  //
  // The new metric must be documented in
  // //tools/metrics/histograms/histograms.xml in the Chromium repository.
  bool SendToUMA(const std::string& name,
                 int sample,
                 int min,
                 int max,
                 int nbuckets) override;

  // Sends linear histogram data to Chrome for transport to UMA and
  // returns true on success. This method results in the equivalent of
  // an asynchronous non-blocking RPC to UMA_HISTOGRAM_ENUMERATION
  // inside Chrome (see base/histogram.h).
  //
  // |sample| is the sample value to be recorded (0 <= |sample| < |max|).
  // |max| is the maximum value of the histogram samples.
  // (-infinity, 0) is the implicit underflow bucket.
  // [|max|,infinity) is the implicit overflow bucket.
  //
  // An enumeration histogram requires |max| + 1 number of
  // buckets. Note that the memory allocated in Chrome for each
  // histogram is proportional to the number of buckets. Therefore, it
  // is strongly recommended to keep this number low (e.g., 50 is
  // normal, while 100 is high).
  //
  // The new metric must be documented in
  // //tools/metrics/histograms/histograms.xml in the Chromium repository.
  bool SendEnumToUMA(const std::string& name, int sample, int max) override;

  // Specialization of SendEnumToUMA for boolean values.
  bool SendBoolToUMA(const std::string& name, bool sample) override;

  // Sends sparse histogram sample to Chrome for transport to UMA.  Returns
  // true on success.
  //
  // |sample| is the 32-bit integer value to be recorded.
  bool SendSparseToUMA(const std::string& name, int sample) override;

  // Sends a user action to Chrome for transport to UMA and returns true on
  // success. This method results in the equivalent of an asynchronous
  // non-blocking RPC to UserMetrics::RecordAction.
  //
  // |action| is the user-generated event (e.g., "MuteKeyPressed").
  //
  // The new metric must be added to AddChromeOSActions() in
  // //tools/metrics/actions/extract_actions.py in the Chromium repository,
  // which should then be run to generate a hash for the new action.
  bool SendUserActionToUMA(const std::string& action) override;

  // Sends a signal to UMA that a crash of the given |crash_kind|
  // has occurred.  Used by UMA to generate stability statistics.
  bool SendCrashToUMA(const char* crash_kind) override;

  // Sends a "generic Chrome OS event" to UMA.  This is an event name
  // that is translated into an enumerated histogram entry.  Event names
  // must first be registered in metrics_library.cc.  See that file for
  // more details.
  bool SendCrosEventToUMA(const std::string& event) override;

#if USE_METRICS_UPLOADER
  // Sends |num_samples| samples with the same value to chrome.
  // Otherwise equivalent to SendToUMA().
  bool SendRepeatedToUMA(const std::string& name,
                         int sample,
                         int min,
                         int max,
                         int nbuckets,
                         int num_samples) override;
#endif

  void SetConsentFileForTest(const base::FilePath& consent_file);

 private:
  friend class CMetricsLibraryTest;
  friend class MetricsLibraryTest;

  // This function is used by tests only to mock the device policies.
  void SetPolicyProvider(policy::PolicyProvider* provider);

  // Time at which we last checked if metrics were enabled.
  static time_t cached_enabled_time_;

  // Cached state of whether or not metrics were enabled.
  static bool cached_enabled_;

  base::FilePath uma_events_file_;
  base::FilePath consent_file_;

  std::unique_ptr<policy::PolicyProvider> policy_provider_;
};

#endif  // METRICS_METRICS_LIBRARY_H_
