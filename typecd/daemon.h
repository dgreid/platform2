// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TYPECD_DAEMON_H_
#define TYPECD_DAEMON_H_

#include <memory>

#include <base/memory/weak_ptr.h>
#include <brillo/daemons/daemon.h>

namespace typecd {

class Daemon : public brillo::Daemon {
 public:
  Daemon();
  ~Daemon() override;

 protected:
  int OnInit() override;

 private:
  base::WeakPtrFactory<Daemon> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(Daemon);
};

}  // namespace typecd

#endif  // TYPECD_DAEMON_H__
