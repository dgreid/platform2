// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The broker daemon that supports Trichecus from within the Chrome OS guest machine.

use std::env;
use std::io::{copy, stdin, stdout};
use std::thread::spawn;

use libchromeos::vsock::VsockCid;
use sirenia::cli::initialize_common_arguments;
use sirenia::transport::{
    ClientTransport, IPClientTransport, Transport, TransportType, VsockClientTransport,
};

fn main() {
    let args: Vec<String> = env::args().collect();
    let config = initialize_common_arguments(&args[1..]).unwrap();
    let mut transport: Box<dyn ClientTransport> = match config.connection_type {
        TransportType::IpConnection(url) => Box::new(IPClientTransport::new(&url).unwrap()),
        _ => Box::new(VsockClientTransport::new(VsockCid::Hypervisor).unwrap()),
    };

    if let Ok(Transport(mut r, mut w)) = transport.connect() {
        // TODO replace this with RPC invocations.
        spawn(move || {
            copy(&mut stdin(), &mut w).unwrap_or(0);
        });
        copy(&mut r, &mut stdout()).unwrap_or(0);
    }
}
