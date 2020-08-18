// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RUNTIME_PROBE_FUNCTIONS_MEMORY_H_
#define RUNTIME_PROBE_FUNCTIONS_MEMORY_H_

#include <memory>
#include <string>
#include <vector>

#include <base/values.h>

#include "runtime_probe/probe_function.h"

namespace runtime_probe {

// Refer to SMBIOS specification.
// https://www.dmtf.org/sites/default/files/standards/documents/DSP0134_3.3.0.pdf
struct DmiMemoryRaw {
  // Header
  uint8_t type;
  uint8_t length;
  uint16_t handle;

  // Memory attributes
  uint8_t pad_1[8];       // skipped values
  uint16_t size;          // bit15: 0=MiB, 1=KiB
  uint8_t pad_2[2];       // skipped values
  uint8_t locator;        // string
  uint8_t pad_3[4];       // skipped values
  uint16_t speed;         // in MHz
  uint8_t manufacturer;   // string
  uint8_t serial_number;  // string
  uint8_t asset_tag;      // string
  uint8_t part_number;    // string
} __attribute__((packed));

struct DmiMemory {
  uint16_t size;
  uint16_t speed;
  std::string locator;
  std::string part_number;
  static std::unique_ptr<DmiMemory> From(const std::vector<uint8_t>& blob);
};

class MemoryFunction : public ProbeFunction {
 public:
  NAME_PROBE_FUNCTION("memory");

  static constexpr auto FromKwargsValue = FromEmptyKwargsValue<MemoryFunction>;

  DataType Eval() const override;

  int EvalInHelper(std::string* output) const override;
};

}  // namespace runtime_probe

#endif  // RUNTIME_PROBE_FUNCTIONS_MEMORY_H_
