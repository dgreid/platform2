// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEBUGD_SRC_CUPS_TOOL_H_
#define DEBUGD_SRC_CUPS_TOOL_H_

#include <stdint.h>

#include <string>
#include <vector>

#include <base/macros.h>

namespace debugd {

class CupsTool {
 public:
  CupsTool() = default;
  CupsTool(const CupsTool&) = delete;
  CupsTool& operator=(const CupsTool&) = delete;

  ~CupsTool() = default;

  // Add a printer that can be configured automatically.
  int32_t AddAutoConfiguredPrinter(const std::string& name,
                                   const std::string& uri);

  // Add a printer configured with the ppd found in |ppd_contents|.
  int32_t AddManuallyConfiguredPrinter(
      const std::string& name,
      const std::string& uri,
      const std::vector<uint8_t>& ppd_contents);

  // Remove a printer from CUPS using lpadmin.
  bool RemovePrinter(const std::string& name);

  // Evaluates true if the |uri| (fed to lpadmin) seems valid.
  // Valid-looking URIs take the form "scheme://host..." for
  // which the trailing port spec is optional. In addition, they must
  // already be appropriately percent-encoded.
  bool UriSeemsReasonable(const std::string& uri);
};

}  // namespace debugd

#endif  // DEBUGD_SRC_CUPS_TOOL_H_
