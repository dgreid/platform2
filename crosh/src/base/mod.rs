// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The base module handles registration of a base set of crosh commands.

mod arc;
mod ccd_pass;
mod dmesg;
mod set_time;
mod verify_ro;
mod vmc;

use crate::dispatcher::Dispatcher;

pub fn register(dispatcher: &mut Dispatcher) {
    arc::register(dispatcher);
    ccd_pass::register(dispatcher);
    dmesg::register(dispatcher);
    set_time::register(dispatcher);
    verify_ro::register(dispatcher);
    vmc::register(dispatcher);
}
