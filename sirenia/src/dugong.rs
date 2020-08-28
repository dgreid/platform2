// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The broker daemon that supports Trichecus from within the Chrome OS guest machine.

use std::env;
use std::io::{copy, stdin, stdout};
use std::thread::spawn;
use sys_util::{info, syslog};

use sirenia::cli::{initialize_common_arguments, CommonConfig};
use sirenia::transport::{
    ClientTransport, IPClientTransport, ReadDebugSend, Transport, TransportType,
    VsockClientTransport, WriteDebugSend,
};

fn main() -> Result<(), sys_util::syslog::Error> {
    let args: Vec<String> = env::args().collect();
    let config = initialize_common_arguments(&args[1..]).unwrap();
    let mut transport = open_connection(config);

    if let Err(e) = syslog::init() {
        eprintln!("failed to initialize syslog: {}", e);
        return Err(e);
    }

    if let Ok(Transport(r, w)) = transport.connect() {
        // TODO replace this with RPC invocations.
        start_rpc(r, w);
    }

    Ok(())
}

fn open_connection(config: CommonConfig) -> Box<dyn ClientTransport> {
    match config.connection_type {
        TransportType::IpConnection(url) => Box::new(IPClientTransport::new(&url).unwrap()),
        TransportType::VsockConnection(url) => Box::new(VsockClientTransport::new(&url).unwrap()),
    }
}

fn start_rpc(mut r: Box<dyn ReadDebugSend>, mut w: Box<dyn WriteDebugSend>) {
    info!("Opening Shell for testing");
    spawn(move || {
        copy(&mut stdin(), &mut w).unwrap_or(0);
    });
    copy(&mut r, &mut stdout()).unwrap_or(0);
}
