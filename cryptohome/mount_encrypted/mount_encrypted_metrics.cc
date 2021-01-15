// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/mount_encrypted/mount_encrypted_metrics.h"

namespace mount_encrypted {

namespace {
static MountEncryptedMetrics* g_metrics = nullptr;

constexpr char kSystemKeyStatus[] = "Platform.MountEncrypted.SystemKeyStatus";
constexpr char kEncryptionKeyStatus[] =
    "Platform.MountEncrypted.EncryptionKeyStatus";
}  // namespace

MountEncryptedMetrics::MountEncryptedMetrics(const std::string& output_file) {
  metrics_library_.SetOutputFile(output_file);
}

// static
void MountEncryptedMetrics::Initialize(const std::string& output_file) {
  DCHECK_EQ(g_metrics, nullptr);
  g_metrics = new MountEncryptedMetrics(output_file);
}

// static
MountEncryptedMetrics* MountEncryptedMetrics::Get() {
  DCHECK_NE(g_metrics, nullptr);
  return g_metrics;
}

// static
void MountEncryptedMetrics::Reset() {
  DCHECK_NE(g_metrics, nullptr);
  delete g_metrics;
}

void MountEncryptedMetrics::ReportSystemKeyStatus(
    EncryptionKey::SystemKeyStatus status) {
  metrics_library_.SendEnumToUMA(
      kSystemKeyStatus, static_cast<int>(status),
      static_cast<int>(EncryptionKey::SystemKeyStatus::kCount));
}

void MountEncryptedMetrics::ReportEncryptionKeyStatus(
    EncryptionKey::EncryptionKeyStatus status) {
  metrics_library_.SendEnumToUMA(
      kEncryptionKeyStatus, static_cast<int>(status),
      static_cast<int>(EncryptionKey::EncryptionKeyStatus::kCount));
}

}  // namespace mount_encrypted