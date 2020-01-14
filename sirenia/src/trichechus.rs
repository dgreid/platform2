// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A TEE application life-cycle manager.

use std::env;
use std::io::copy;
use std::thread::spawn;

use sirenia::cli::{initialize_common_arguments, TransportType};
use sirenia::to_sys_util;
use sirenia::transport::{IPServerTransport, ServerTransport, VsockServerTransport};

fn main() {
    let args: Vec<String> = env::args().collect();
    let config = initialize_common_arguments(&args[1..]).unwrap();

    to_sys_util::block_all_signals();
    // This is safe because no additional file descriptors have been opened.
    let ret = unsafe { to_sys_util::fork() }.unwrap();
    if ret != 0 {
        // The parent process collects the return codes from the child processes, so they do not
        // remain zombies.
        while to_sys_util::wait_for_child() {}
        println!("Reaper done!");
        return;
    }

    // Unblock signals for the process that spawns the children. It might make sense to fork
    // again here for each child to avoid them blocking each other.
    to_sys_util::unblock_all_signals();

    // Spawn children here.
    println!("Fork done!");

    let mut transport: Box<dyn ServerTransport> = match config.connection_type {
        TransportType::IpConnection(url) => Box::new(IPServerTransport::new(&url).unwrap()),
        _ => Box::new(VsockServerTransport::new().unwrap()),
    };

    // Handle incoming connections.
    loop {
        if let Ok(mut t) = transport.accept() {
            spawn(move || {
                // TODO replace this with a RPC handler.
                copy(&mut t.0, &mut t.1).unwrap_or(0);
            });
        }
    }
}
