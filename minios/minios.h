// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MINIOS_MINIOS_H_
#define MINIOS_MINIOS_H_

#include "minios/process_manager.h"
#include "minios/screens.h"

class MiniOs {
 public:
  MiniOs() = default;
  ~MiniOs() = default;

  // Runs the miniOS flow.
  int Run();

 private:
  MiniOs(const MiniOs&) = delete;
  MiniOs& operator=(const MiniOs&) = delete;
  screens::Screens screens_;
};

#endif  // MINIOS_MINIOS_H__
