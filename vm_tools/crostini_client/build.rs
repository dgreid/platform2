// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::env;
use std::fmt::Write as FmtWrite;
use std::fs;
use std::io::Write;
use std::path::PathBuf;

fn main() {
    let out_dir = PathBuf::from(env::var("OUT_DIR").unwrap());
    let proto_root = match env::var("SYSROOT") {
        Ok(dir) => PathBuf::from(dir).join("usr/include/chromeos"),
        // Make this work when typing "cargo build" in platform2/vm_tools/crostini_client
        Err(_) => PathBuf::from("../../system_api"),
    };
    let concierge_dir = proto_root.join("dbus/vm_concierge");
    let cicerone_dir = proto_root.join("dbus/vm_cicerone");
    let dlcservice_dir = proto_root.join("dbus/dlcservice");
    let seneschal_dir = proto_root.join("dbus/seneschal");
    let vmplugin_dispatcher_dir = proto_root.join("dbus/vm_plugin_dispatcher");
    let input_files = [
        concierge_dir.join("concierge_service.proto"),
        cicerone_dir.join("cicerone_service.proto"),
        dlcservice_dir.join("dlcservice.proto"),
        seneschal_dir.join("seneschal_service.proto"),
        vmplugin_dispatcher_dir.join("vm_plugin_dispatcher.proto"),
    ];
    let include_dirs = [
        concierge_dir,
        cicerone_dir,
        dlcservice_dir,
        seneschal_dir,
        vmplugin_dispatcher_dir,
    ];

    protoc_rust::Codegen::new()
        .out_dir(&out_dir)
        .inputs(&input_files)
        .includes(&include_dirs)
        .run()
        .expect("protoc");

    let mut path_include_mods = String::new();
    for input_file in input_files.iter() {
        let stem = input_file.file_stem().unwrap().to_str().unwrap();
        let mod_path = out_dir.join(format!("{}.rs", stem));
        writeln!(
            &mut path_include_mods,
            "#[path = \"{}\"]",
            mod_path.display()
        )
        .unwrap();
        writeln!(&mut path_include_mods, "pub mod {};", stem).unwrap();
    }

    let mut mod_out = fs::File::create(out_dir.join("proto_include.rs")).unwrap();
    writeln!(mod_out, "pub mod system_api {{\n{}}}", path_include_mods).unwrap();
}
