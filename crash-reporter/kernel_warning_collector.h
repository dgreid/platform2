// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The kernel warning collector gathers logs from kernel warnings.
// Anomaly detector runs the kernel warning collector when it detects strings
// matching the expected warning pattern in /var/log/messages.

#ifndef CRASH_REPORTER_KERNEL_WARNING_COLLECTOR_H_
#define CRASH_REPORTER_KERNEL_WARNING_COLLECTOR_H_

#include <string>

#include <base/macros.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST

#include "crash-reporter/crash_collector.h"

// Kernel warning collector.
class KernelWarningCollector : public CrashCollector {
 public:
  enum WarningType {
    kGeneric,
    kWifi,
    kSMMUFault,
    kSuspend,
    // Iwlwifi is the name of Intel WiFi driver that we want to collect its
    // error dumps.
    kIwlwifi,
  };

  KernelWarningCollector();

  ~KernelWarningCollector() override;

  // Collects warning.
  bool Collect(WarningType type);

 protected:
  std::string warning_report_path_;

 private:
  friend class KernelWarningCollectorTest;
  FRIEND_TEST(KernelWarningCollectorTest, CollectOK);

  // Reads the full content of the kernel warn dump and its signature.
  bool LoadKernelWarning(std::string* content,
                         std::string* signature,
                         std::string* func_name,
                         WarningType type);
  bool ExtractSignature(const std::string& content,
                        std::string* signature,
                        std::string* func_name);
  bool ExtractIwlwifiSignature(const std::string& content,
                               std::string* signature,
                               std::string* func_name);
  bool ExtractSMMUFaultSignature(const std::string& content,
                                 std::string* signature,
                                 std::string* func_name);

  DISALLOW_COPY_AND_ASSIGN(KernelWarningCollector);
};

#endif  // CRASH_REPORTER_KERNEL_WARNING_COLLECTOR_H_
