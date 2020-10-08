// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The broker daemon that supports Trichecus from within the Chrome OS guest machine.

use std::env;
use std::io::{copy, stdin, stdout};
use std::thread::spawn;
use sys_util::{error, info, syslog};

use sirenia::build_info::BUILD_TIMESTAMP;
use sirenia::cli::initialize_common_arguments;
use sirenia::communication::{read_message, write_message, AppInfo, Request, Response};
use sirenia::transport::{
    ClientTransport, IPClientTransport, ReadDebugSend, Transport, TransportType,
    VsockClientTransport, WriteDebugSend,
};

fn main() -> Result<(), sys_util::syslog::Error> {
    let args: Vec<String> = env::args().collect();
    let config = initialize_common_arguments(&args[1..]).unwrap();
    let transport_type = config.connection_type;
    let mut transport = open_connection(&transport_type);

    if let Err(e) = syslog::init() {
        eprintln!("failed to initialize syslog: {}", e);
        return Err(e);
    }

    info!("Starting dugong: {}", BUILD_TIMESTAMP);

    if let Ok(Transport(r, w)) = transport.connect() {
        start_rpc(transport_type, r, w);
    } else {
        error!("transport connect failed");
    }

    Ok(())
}

fn open_connection(connection_type: &TransportType) -> Box<dyn ClientTransport> {
    match connection_type {
        TransportType::IpConnection(url) => Box::new(IPClientTransport::new(&url).unwrap()),
        TransportType::VsockConnection(url) => Box::new(VsockClientTransport::new(&url).unwrap()),
    }
}

fn start_rpc(transport_type: TransportType, r: Box<dyn ReadDebugSend>, w: Box<dyn WriteDebugSend>) {
    info!("Opening connection to trichechus");
    // Right now just send the message to start up the shell and print and log
    // responses from trichechus
    let child = spawn(move || {
        start_logger(r, transport_type);
    });
    // TODO: Need a way of keeping the write end while also waiting for calls from
    // other processes that want to use the TEE, but for now will just ask to
    // spin up the shell process.
    info!("Requesting startup of shell app");
    request_start_tee_app(w, "shell");

    child.join().unwrap();
}

fn request_start_tee_app(mut w: Box<dyn WriteDebugSend>, app_id: &str) {
    // TODO: Need to bind to the new port to prevent other processes from using
    // it, but need to add the option to bind to an ephemeral port in vsock
    let app_info = AppInfo {
        app_id: String::from(app_id),
        port_number: 0, // TODO: Will use this later
    };
    match write_message(&mut w, Request::StartSession(app_info)) {
        Ok(()) => (),
        Err(e) => error!("Error writing: {}", e),
    }

    // TODO: This function should also set up the communication between the TEE
    // and the calling process, for now it will just connect the write end of
    // the TEE to stdin of dugong and the write end of the TEE will just
    // write messages to dugong using serialization.
}

fn start_logger(mut r: Box<dyn ReadDebugSend>, transport_type: TransportType) {
    loop {
        let message = read_message(&mut r).unwrap();
        match message {
            Response::StartConnection => {
                info!("Starting the connection with TEE app");
                setup_shell_stdin(&transport_type);
            }
            Response::LogInfo(s) => {
                info!("{}", s);
                println!("{}", s);
            }
            Response::LogError(s) => {
                error!("{}", s);
                println!("{}", s);
            }
        }
    }
}

fn setup_shell_stdin(transport_type: &TransportType) {
    let mut transport = open_connection(transport_type);
    if let Ok(Transport(mut r, mut w)) = transport.connect() {
        let child1 = spawn(move || {
            copy(&mut stdin(), &mut w).unwrap_or(0);
        });
        let child2 = spawn(move || {
            copy(&mut r, &mut stdout()).unwrap_or(0);
        });
        child1.join().unwrap();
        child2.join().unwrap();
    }
}

// Handles a request to start up and connect a TEE app. Checks the permissions
// of the caller. TODO: Actually implement this.
pub fn start_app() {}
