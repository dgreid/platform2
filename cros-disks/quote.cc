// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/quote.h"

#include <iomanip>

namespace cros_disks {

static const char kRedacted[] = "(redacted)";

std::ostream& operator<<(std::ostream& out, Quoter<const char*> quoter) {
  const char* const s = quoter.ref;
  if (!s)
    return out << "(null)";

  if (quoter.redacted)
    return out << kRedacted;

  return out << std::quoted(s, '\'');
}

std::ostream& operator<<(std::ostream& out, Quoter<std::string> quoter) {
  if (quoter.redacted)
    return out << kRedacted;

  return out << std::quoted(quoter.ref, '\'');
}

std::ostream& operator<<(std::ostream& out, Quoter<base::FilePath> quoter) {
  if (quoter.redacted)
    return out << kRedacted;

  return out << std::quoted(quoter.ref.value(), '\'');
}

}  // namespace cros_disks
