// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A TEE application life-cycle manager.

use std::env;
use std::fmt::{self, Debug, Display};
use std::io::{BufRead, BufReader};
use std::os::unix::io::AsRawFd;
use std::path::Path;
use std::string::String;
use std::thread::spawn;

use sirenia::cli::initialize_common_arguments;
use sirenia::communication::{self, get_app_path, read_message, write_message, Request, Response};
use sirenia::sandbox::{self, Sandbox};
use sirenia::to_sys_util;
use sirenia::transport::{
    IPServerTransport, ReadDebugSend, ServerTransport, Transport, TransportType,
    VsockServerTransport, WriteDebugSend,
};
use sys_util::{self, error, info, pipe, syslog};

#[derive(Debug)]
pub enum Error {
    /// Error initializing the syslog.
    InitSyslog(sys_util::syslog::Error),
    /// Error opening a pipe.
    OpenPipe(sys_util::Error),
    /// Error Creating a new sandbox.
    NewSandbox(sandbox::Error),
    /// Error starting up a sandbox.
    RunSandbox(sandbox::Error),
    /// Error getting the path for an app id.
    AppIdPathError(communication::Error),
}

impl Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        use self::Error::*;

        match self {
            InitSyslog(e) => write!(f, "failed to initialize the syslog: {}", e),
            OpenPipe(e) => write!(f, "failed to open pipe: {}", e),
            NewSandbox(e) => write!(f, "failed to create new sandbox: {}", e),
            RunSandbox(e) => write!(f, "failed to start up sandbox: {}", e),
            AppIdPathError(e) => write!(f, "failed to get path for the app id: {}", e),
        }
    }
}

/// The result of an operation in this crate.
pub type Result<T> = std::result::Result<T, Error>;

// TODO: Should these be macros? What is the advantage of macros?
fn log_error(w: &mut Box<dyn WriteDebugSend>, s: String) {
    error!("{}", &s);
    let err = write_message(w, Response::LogError(format!("Trichechus error: {}", s)));

    if let Err(e) = err {
        error!("{}", e)
    }
}

fn log_info(w: &mut Box<dyn WriteDebugSend>, s: String) {
    info!("{}", &s);
    let err = write_message(w, Response::LogInfo(format!("Trichechus info: {}", s)));

    if let Err(e) = err {
        error!("{}", e)
    }
}

// TODO: Figure out how to clean up TEEs that are no longer in use
// TODO: Figure out rate limiting and prevention against DOS attacks
// TODO: What happens if dugong crashes? How do we want to handle
fn main() -> Result<()> {
    if let Err(e) = syslog::init() {
        eprintln!("failed to initialize syslog: {}", e);
        return Err(Error::InitSyslog(e));
    }

    info!("Starting trichechus");
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
        return Ok(());
    }

    // Unblock signals for the process that spawns the children. It might make sense to fork
    // again here for each child to avoid them blocking each other.
    to_sys_util::unblock_all_signals();

    let mut transport: Box<dyn ServerTransport> = match config.connection_type {
        TransportType::IpConnection(url) => Box::new(IPServerTransport::new(&url).unwrap()),
        TransportType::VsockConnection(url) => Box::new(VsockServerTransport::new(&url).unwrap()),
    };

    // Handle parent dugong connection.
    info!("Waiting for connection");
    if let Ok(Transport(mut r, mut w)) = transport.accept() {
        log_info(&mut w, "Accepted connection".to_string());
        loop {
            match read_message(&mut r) {
                Ok(message) => handle_message(&mut w, message, &mut transport),
                Err(e) => log_error(&mut w, e.to_string()),
            }
        }
    }

    Ok(())
}

// Handles an incoming message from dugong. TODO: Is it fine that this takes
// in a Request, while read_message only guarantees returning something that
// implements the Deserialize trait
fn handle_message(
    mut w: &mut Box<dyn WriteDebugSend>,
    message: Request,
    mut transport: &mut Box<dyn ServerTransport>,
) {
    if let Request::StartSession(app_info) = message {
        log_info(
            &mut w,
            format!(
                "Received start session message with app_id: {}",
                app_info.app_id
            ),
        );
        start_tee_app(&mut w, &app_info.app_id, &mut transport);
    }
}

// Starts up the TEE application that was requested from Dugong and sends a
// message back to dugong to connect a new socket to communcate with the TEE.
fn start_tee_app(
    mut w: &mut Box<dyn WriteDebugSend>,
    process: &str,
    transport: &mut Box<dyn ServerTransport>,
) {
    if let Err(e) = write_message(&mut w, Response::StartConnection) {
        log_error(&mut w, e.to_string());
        return;
    }

    // TODO: Timeout and retry accept and check port number
    let Transport(mut tee_r, mut tee_w) = transport.accept().unwrap();
    // TODO: Eventually will need to spawn this in a separate process, but the
    // output of the tee will have to be written somewhere else first, otherwise
    // the main trichechus process and the tee process will both have mutable
    // borrows of the write end of the tee process.
    match start_tee_app_spawn(&mut w, &process, &mut tee_r, &mut tee_w) {
        Ok(_) => (),
        Err(e) => log_error(w, e.to_string()),
    }
}

fn start_tee_app_spawn(
    mut w: &mut Box<dyn WriteDebugSend>,
    process: &str,
    tee_r: &mut Box<dyn ReadDebugSend>,
    tee_w: &mut Box<dyn WriteDebugSend>,
) -> Result<()> {
    let (pipe_r, pipe_w) = pipe(false).map_err(Error::OpenPipe)?;
    let mut sandbox = Sandbox::new(None).map_err(Error::NewSandbox)?;
    let process_path = get_app_path(process).map_err(Error::AppIdPathError)?;

    sandbox
        .run(
            Path::new(process_path),
            &[process_path],
            tee_r.as_raw_fd(),
            tee_w.as_raw_fd(),
            pipe_w.as_raw_fd(),
        )
        .map_err(Error::RunSandbox)?;

    let mut reader = BufReader::new(pipe_r);
    log_info(&mut w, "Started shell\n".to_string());

    loop {
        let mut s = String::new();
        let bytes_read = reader.read_line(&mut s).unwrap();
        if bytes_read == 0 {
            break;
        }
        log_info(&mut w, s);
    }

    let result = sandbox.wait_for_completion();

    if result.is_err() {
        log_error(w, format!("Got error code: {:?}", result));
    }

    result.unwrap();

    Ok(())
}
