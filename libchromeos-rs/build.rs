// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Link to vboot reference library for access to crossystem.

#[cfg(feature = "chromeos-module")]
fn include_chromeos_module_deps() {
    pkg_config::Config::new().probe("vboot_host").unwrap();
}

fn main() {
    #[cfg(feature = "chromeos-module")]
    include_chromeos_module_deps();

    println!("cargo:rerun-if-changed=build.rs");
}
