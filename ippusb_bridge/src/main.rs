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
use std::io::{self, Read, Write};
use std::net::TcpListener;
use std::os::unix::io::IntoRawFd;
use std::os::unix::net::{UnixListener, UnixStream};
use std::sync::atomic::{AtomicBool, AtomicI32, Ordering};
use std::sync::Arc;
use std::time::{Duration, Instant};

use sync::Mutex;
use sys_util::{error, info, register_signal_handler, syslog, EventFd, PollContext, PollToken};
use tiny_http::{ClientConnection, Stream};

use crate::arguments::Args;
use crate::http::handle_request;
use crate::listeners::{Accept, ScopedUnixListener};
use crate::usb_connector::{UnplugDetector, UsbConnector};
use crate::util::ConnectionTracker;

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
    keep_alive_socket: Option<ScopedUnixListener>,

    /// The last time a keep-alive message was received.
    last_keep_alive: Arc<Mutex<Instant>>,

    /// Responsible for tracking the number of active clients, and waking the poll loop when the
    /// number of clients changes from zero to non-zero, or from non-zero to zero.
    connection_tracker: Arc<Mutex<ConnectionTracker>>,

    /// True if Daemon currently has no clients connected.
    idle: bool,

    /// The last time Daemon had clients connected.
    last_activity_time: Instant,
}

impl<A: Accept> Daemon<A> {
    fn new(
        shutdown: EventFd,
        listener: A,
        usb: UsbConnector,
        keep_alive_socket: Option<ScopedUnixListener>,
    ) -> Result<Self> {
        Ok(Self {
            shutdown,
            listener,
            usb,
            keep_alive_socket,
            last_keep_alive: Arc::new(Mutex::new(Instant::now())),
            connection_tracker: Arc::new(Mutex::new(
                ConnectionTracker::new().map_err(Error::EventFd)?,
            )),
            idle: true,
            last_activity_time: Instant::now(),
        })
    }

    fn run(&mut self) -> Result<()> {
        #[derive(PollToken)]
        enum Token {
            Shutdown,
            ClientConnection,
            KeepAliveConnection,
            IdleStateChanged,
        }

        let poll_ctx: PollContext<Token> = PollContext::build_with(&[
            (&self.shutdown, Token::Shutdown),
            (&self.listener, Token::ClientConnection),
        ])
        .map_err(Error::SysUtil)?;

        if let Some(socket) = self.keep_alive_socket.as_ref() {
            poll_ctx
                .add(socket, Token::KeepAliveConnection)
                .map_err(Error::SysUtil)?;

            poll_ctx
                .add(
                    self.connection_tracker.lock().event_fd(),
                    Token::IdleStateChanged,
                )
                .map_err(Error::SysUtil)?;
        }

        'poll: loop {
            // poll_timeout will return None if our timeout has elapsed.
            let timeout = match self.poll_timeout() {
                Some(timeout) => timeout,
                None => break 'poll,
            };

            let events = poll_ctx.wait_timeout(timeout).map_err(Error::PollEvents)?;
            for event in &events {
                match event.token() {
                    Token::Shutdown => break 'poll,
                    Token::ClientConnection => match self.listener.accept() {
                        Ok(stream) => self.handle_connection(stream),
                        Err(err) => error!("Failed to accept connection: {}", err),
                    },
                    Token::KeepAliveConnection => {
                        let socket = self.keep_alive_socket.as_ref().unwrap();
                        match UnixListener::accept(socket) {
                            Ok((stream, _)) => self.handle_keep_alive(stream),
                            Err(err) => error!("Failed to accept keep-alive connection: {}", err),
                        }
                    }
                    Token::IdleStateChanged => self.update_idle_state(),
                }
            }
        }
        Ok(())
    }

    /// Handles updating self.idle and self.last_activity_time when we receive
    /// an IdleStateChanged event from the connection_tracker.
    fn update_idle_state(&mut self) {
        let connection_tracker = self.connection_tracker.lock();
        self.idle = connection_tracker.active_connections() == 0;
        // Clear the event from the EventFd.
        if let Err(e) = connection_tracker.event_fd().read() {
            error!("Failed to read from ConnectionTracker event fd: {}", e);
        }
        if self.idle {
            self.last_activity_time = Instant::now();
        }
    }

    /// Calculates the poll timeout to use based on
    /// * If timeouts are enabled (only true if a keep-alive socket was provided)
    /// * The number of connected clients
    /// * The last time a client was connected
    /// * The last time a keep-alive was received.
    ///
    /// If we've exceeded our activity timeout, returns None, indicating that the
    /// poll loop should shut down.
    fn poll_timeout(&self) -> Option<Duration> {
        let activity_timeout = Duration::from_secs(10);
        // If we have a keep-alive socket, the poll loop's behavior changes from running
        // indefinitely to shutting down after 10 seconds of inactivity.
        //
        // If any clients are connected, we consider ippusb_bridge to be active. Once all
        // clients have disconnected, we record the time in last_activity_time. We also record
        // the last time that a keep-alive was received as last_keep_alive.
        if self.idle && self.keep_alive_socket.is_some() {
            let last_keep_alive = self.last_keep_alive.lock();
            let elapsed = std::cmp::max(*last_keep_alive, self.last_activity_time).elapsed();

            // If 10 seconds have elapsed since the later of last_keep_alive and
            // last_activity_time, we shut down.
            if elapsed >= activity_timeout {
                SHUTDOWN.store(true, Ordering::Relaxed);
                None
            } else {
                Some(activity_timeout - elapsed)
            }
        } else {
            // Use an infinite timeout, because either we aren't using a keep-alive connection,
            // or there are currently active client connections. Once the last client leaves,
            // we'll get an IdleStateChanged event and can switch to a finite timeout.
            Some(Duration::new(i64::MAX as u64, 0))
        }
    }

    fn handle_keep_alive(&mut self, mut stream: UnixStream) {
        let thread_last_keep_alive = self.last_keep_alive.clone();
        std::thread::spawn(move || {
            let mut buf = [0u8; 12];
            match stream.read_exact(&mut buf) {
                Ok(()) if &buf == "\x0bkeep-alive\0".as_bytes() => {
                    info!("Got keep alive message");
                    *thread_last_keep_alive.lock() = Instant::now();
                    if let Err(e) = stream.write_all(b"\x04ack\0") {
                        error!("Failed to send keep-alive ack: {}", e);
                    }
                }
                Ok(()) => {
                    error!("Unexpected message on keep-alive socket: {:?}", &buf);
                }
                Err(e) => {
                    error!("Failed to read from keep-alive socket: {}", e);
                }
            }
        });
    }

    fn handle_connection(&mut self, stream: Stream) {
        let connection = ClientConnection::new(stream);
        let mut thread_usb = self.usb.clone();
        let thread_connection_tracker = self.connection_tracker.clone();
        std::thread::spawn(move || {
            thread_connection_tracker.lock().client_connected();
            for request in connection {
                let usb_conn = thread_usb.get_connection();
                if let Err(e) = handle_request(usb_conn, request) {
                    error!("Handling request failed: {}", e);
                }
            }
            thread_connection_tracker.lock().client_disconnected();
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

    let keep_alive_socket = args
        .keep_alive
        .map(|keep_alive_path| {
            info!("Polling for keep-alive on {}", keep_alive_path.display());
            let keep_alive_listener =
                UnixListener::bind(keep_alive_path).map_err(Error::CreateSocket)?;
            Ok(ScopedUnixListener(keep_alive_listener))
        })
        .transpose()?;

    let usb = UsbConnector::new(args.bus_device).map_err(Error::CreateUsbConnector)?;
    let unplug_shutdown_fd = shutdown_fd.try_clone().map_err(Error::EventFd)?;
    let _unplug = UnplugDetector::new(usb.device(), unplug_shutdown_fd, &SHUTDOWN);

    if let Some(unix_socket_path) = args.unix_socket {
        info!("Listening on {}", unix_socket_path.display());
        let unix_listener =
            ScopedUnixListener(UnixListener::bind(unix_socket_path).map_err(Error::CreateSocket)?);
        let mut daemon = Daemon::new(shutdown_fd, unix_listener, usb, keep_alive_socket)?;
        daemon.run()?;
    } else {
        let host = "127.0.0.1:60000";
        info!("Listening on {}", host);
        let tcp_listener = TcpListener::bind(host).map_err(Error::CreateSocket)?;
        let mut daemon = Daemon::new(shutdown_fd, tcp_listener, usb, keep_alive_socket)?;
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
