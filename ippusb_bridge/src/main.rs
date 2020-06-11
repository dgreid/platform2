// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod arguments;
mod http;
mod io_adapters;
mod listeners;
mod usb_connector;
mod util;

use std::fmt;
use std::io;
use std::net::TcpListener;
use std::os::unix::io::IntoRawFd;
use std::os::unix::net::UnixListener;
use std::sync::atomic::{AtomicBool, AtomicI32, Ordering};

use sys_util::{error, info, register_signal_handler, syslog, EventFd, PollContext, PollToken};
use tiny_http::{ClientConnection, Stream};

use crate::arguments::Args;
use crate::http::handle_request;
use crate::listeners::{Accept, ScopedUnixListener};
use crate::usb_connector::{UnplugDetector, UsbConnector};

#[derive(Debug)]
pub enum Error {
    CreateSocket(io::Error),
    CreateUsbConnector(usb_connector::Error),
    EventFd(sys_util::Error),
    ParseArgs(arguments::Error),
    PollEvents(sys_util::Error),
    RegisterHandler(sys_util::Error),
    Syslog(syslog::Error),
    SysUtil(sys_util::Error),
}

impl std::error::Error for Error {}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        use Error::*;
        match self {
            CreateSocket(err) => write!(f, "Failed to create socket: {}", err),
            CreateUsbConnector(err) => write!(f, "Failed to create USB connector: {}", err),
            EventFd(err) => write!(f, "Failed to create/duplicate EventFd: {}", err),
            ParseArgs(err) => write!(f, "Failed to parse arguments: {}", err),
            PollEvents(err) => write!(f, "Failed to poll for events: {}", err),
            RegisterHandler(err) => write!(f, "Registering SIGINT handler failed: {}", err),
            Syslog(err) => write!(f, "Failed to initalize syslog: {}", err),
            SysUtil(err) => write!(f, "Sysutil error: {}", err),
        }
    }
}

type Result<T> = std::result::Result<T, Error>;

// Set to true if the program should terminate.
static SHUTDOWN: AtomicBool = AtomicBool::new(false);

// Holds a raw EventFD with 'static lifetime that can be used to wake up any
// polling threads.
static SHUTDOWN_FD: AtomicI32 = AtomicI32::new(-1);

extern "C" fn sigint_handler() {
    // Check if we've already received one SIGINT. If we have, the program may be misbehaving and
    // not terminating, so to be safe we'll forcefully exit.
    if SHUTDOWN.load(Ordering::Relaxed) {
        std::process::exit(1);
    }
    SHUTDOWN.store(true, Ordering::Relaxed);
    let fd = SHUTDOWN_FD.load(Ordering::Relaxed);
    if fd >= 0 {
        let buf = &1u64 as *const u64 as *const libc::c_void;
        let size = std::mem::size_of::<u64>();
        unsafe { libc::write(fd, buf, size) };
    }
}

/// Registers a SIGINT handler that, when triggered, will write to `shutdown_fd`
/// to notify any listeners of a pending shutdown.
fn add_sigint_handler(shutdown_fd: EventFd) -> sys_util::Result<()> {
    // Leak our copy of the fd to ensure SHUTDOWN_FD remains valid until ippusb_bridge closes, so
    // that we aren't inadvertently writing to an invalid FD in the SIGINT handler. The FD will be
    // reclaimed by the OS once our process has stopped.
    SHUTDOWN_FD.store(shutdown_fd.into_raw_fd(), Ordering::Relaxed);

    const SIGINT: libc::c_int = 2;
    // Safe because sigint_handler is an extern "C" function that only performs
    // async signal-safe operations.
    unsafe { register_signal_handler(SIGINT, sigint_handler) }
}

struct Daemon<A: Accept> {
    shutdown: EventFd,
    listener: A,
    usb: UsbConnector,
}

impl<A: Accept> Daemon<A> {
    fn new(shutdown: EventFd, listener: A, usb: UsbConnector) -> Self {
        Self {
            shutdown,
            listener,
            usb,
        }
    }

    fn run(&mut self) -> Result<()> {
        #[derive(PollToken)]
        enum Token {
            Shutdown,
            ClientConnection,
        }

        let poll_ctx: PollContext<Token> = PollContext::build_with(&[
            (&self.shutdown, Token::Shutdown),
            (&self.listener, Token::ClientConnection),
        ])
        .map_err(Error::SysUtil)?;

        'poll: loop {
            let events = poll_ctx.wait().map_err(Error::PollEvents)?;
            for event in &events {
                match event.token() {
                    Token::Shutdown => break 'poll,
                    Token::ClientConnection => match self.listener.accept() {
                        Ok(stream) => self.handle_connection(stream),
                        Err(err) => error!("Failed to accept connection: {}", err),
                    },
                }
            }
        }
        Ok(())
    }

    fn handle_connection(&mut self, stream: Stream) {
        let connection = ClientConnection::new(stream);
        let mut thread_usb = self.usb.clone();
        std::thread::spawn(move || {
            for request in connection {
                let usb_conn = thread_usb.get_connection();
                if let Err(e) = handle_request(usb_conn, request) {
                    error!("Handling request failed: {}", e);
                }
            }
        });
    }
}

fn run() -> Result<()> {
    syslog::init().map_err(Error::Syslog)?;
    let argv: Vec<String> = std::env::args().collect();
    let args = match Args::parse(&argv).map_err(Error::ParseArgs)? {
        None => return Ok(()),
        Some(args) => args,
    };

    let shutdown_fd = EventFd::new().map_err(Error::EventFd)?;
    let sigint_shutdown_fd = shutdown_fd.try_clone().map_err(Error::EventFd)?;
    add_sigint_handler(sigint_shutdown_fd).map_err(Error::RegisterHandler)?;

    let usb = UsbConnector::new(args.bus_device).map_err(Error::CreateUsbConnector)?;
    let unplug_shutdown_fd = shutdown_fd.try_clone().map_err(Error::EventFd)?;
    let _unplug = UnplugDetector::new(usb.device(), unplug_shutdown_fd, &SHUTDOWN);

    if let Some(unix_socket_path) = args.unix_socket {
        info!("Listening on {}", unix_socket_path.display());
        let unix_listener =
            ScopedUnixListener(UnixListener::bind(unix_socket_path).map_err(Error::CreateSocket)?);
        let mut daemon = Daemon::new(shutdown_fd, unix_listener, usb);
        daemon.run()?;
    } else {
        let host = "127.0.0.1:60000";
        info!("Listening on {}", host);
        let tcp_listener = TcpListener::bind(host).map_err(Error::CreateSocket)?;
        let mut daemon = Daemon::new(shutdown_fd, tcp_listener, usb);
        daemon.run()?;
    }

    info!("Shutting down.");
    Ok(())
}

fn main() {
    // Use run() instead of returning a Result from main() so that we can print
    // errors using Display instead of Debug.
    if let Err(e) = run() {
        error!("{}", e);
    }
}
