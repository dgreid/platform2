// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Generates the Rust D-Bus bindings for sirenia.

use std::path::Path;

use chromeos_dbus_bindings::{self, generate_module};

// The parent path of sirenia.
const SOURCE_DIR: &str = ".";

// (<module name>, <relative path to source xml>)
const BINDINGS_TO_GENERATE: &[(&str, &str)] = &[(
    "org_chromium_manatee",
    "dbus_bindings/org.chromium.ManaTEE1.xml",
)];

fn main() {
    generate_module(Path::new(SOURCE_DIR), BINDINGS_TO_GENERATE).unwrap();
}
