// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Generates the Rust D-Bus bindings for sirenia.

use std::fs::write;
use std::path::Path;

use chromeos_dbus_bindings::{self, generate_module, BindingsType};
use chrono::offset::Utc;

// The parent path of sirenia.
const SOURCE_DIR: &str = ".";

// (<module name>, <relative path to source xml>)
const BINDINGS_TO_GENERATE: &[(&str, &str, BindingsType)] = &[(
    "org_chromium_manatee",
    "dbus_bindings/org.chromium.ManaTEE1.xml",
    BindingsType::Server,
)];

fn main() {
    let source_path = Path::new(SOURCE_DIR);
    write(
        source_path.join("src").join("build_info.rs"),
        format!(
            "pub const BUILD_TIMESTAMP: &str = \"{}\";\n",
            Utc::now().to_rfc3339()
        ),
    )
    .expect("Failed to generate build_info.rs.");
    generate_module(source_path, BINDINGS_TO_GENERATE).unwrap();
}
