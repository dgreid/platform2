// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CROSLOG_VIEWER_JOURNAL_H_
#define CROSLOG_VIEWER_JOURNAL_H_

#include "croslog/config.h"

namespace croslog {

class ViewerJournal {
 public:
  ViewerJournal() = default;

  // Run the plaintext viewer. This may run the runloop to retrieve update
  // events.
  bool Run(const croslog::Config& config);

 private:
  DISALLOW_COPY_AND_ASSIGN(ViewerJournal);
};

}  // namespace croslog

#endif  // CROSLOG_VIEWER_JOURNAL_H_
