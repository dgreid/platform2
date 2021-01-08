// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A TEE application life-cycle manager.

use std::cell::RefCell;
use std::collections::{HashMap, VecDeque};
use std::env;
use std::fmt::Debug;
use std::mem::swap;
use std::os::unix::io::RawFd;
use std::path::{Path, PathBuf};
use std::rc::Rc;
use std::result::Result as StdResult;

use getopts::Options;
use libchromeos::linux::{getpid, getsid, setsid};
use libsirenia::linux::events::{AddEventSourceMutator, EventMultiplexer, Mutator};
use libsirenia::linux::syslog::{Syslog, SyslogReceiverMut, SYSLOG_PATH};
use libsirenia::rpc::{ConnectionHandler, RpcDispatcher, TransportServer};
use libsirenia::sandbox::{self, Sandbox};
use libsirenia::to_sys_util;
use libsirenia::transport::{
    self, create_transport_from_pipes, Transport, TransportType, CROS_CONNECTION_ERR_FD,
    CROS_CONNECTION_R_FD, CROS_CONNECTION_W_FD, DEFAULT_CLIENT_PORT, DEFAULT_CONNECTION_R_FD,
    DEFAULT_CONNECTION_W_FD, DEFAULT_SERVER_PORT,
};
use sirenia::build_info::BUILD_TIMESTAMP;
use sirenia::cli::initialize_common_arguments;
use sirenia::communication::{AppInfo, Trichechus, TrichechusServer};
use sys_util::{self, error, info, syslog};
use thiserror::Error as ThisError;

const SYSLOG_PATH_SHORT_NAME: &str = "L";

#[derive(ThisError, Debug)]
pub enum Error {
    /// Error initializing the syslog.
    #[error("failed to initialize the syslog: {0}")]
    InitSyslog(sys_util::syslog::Error),
    /// Error opening a pipe.
    #[error("failed to open pipe: {0}")]
    OpenPipe(sys_util::Error),
    /// Error creating the transport.
    #[error("failed create transport: {0}")]
    NewTransport(transport::Error),
    /// Got an unexpected connection type
    #[error("got unexpected transport type: {0:?}")]
    UnexpectedConnectionType(TransportType),
    /// Error Creating a new sandbox.
    #[error("failed to create new sandbox: {0}")]
    NewSandbox(sandbox::Error),
    /// Error starting up a sandbox.
    #[error("failed to start up sandbox: {0}")]
    RunSandbox(sandbox::Error),
    /// Got a request type that wasn't expected by the handler.
    #[error("received unexpected request type")]
    UnexpectedRequest,
    /// Invalid app id.
    #[error("invalid app id: {0}")]
    InvalidAppId(String),
}

/// The result of an operation in this crate.
pub type Result<T> = StdResult<T, Error>;

/* Holds the trichechus-relevant information for a TEEApp. */
struct TEEApp {
    sandbox: Sandbox,
    transport: Transport,
}

struct TrichechusState {
    expected_port: u32,
    pending_apps: HashMap<TransportType, String>,
    running_apps: HashMap<TransportType, TEEApp>,
    log_queue: VecDeque<String>,
}

impl TrichechusState {
    fn new() -> Self {
        TrichechusState {
            expected_port: DEFAULT_CLIENT_PORT,
            pending_apps: HashMap::new(),
            running_apps: HashMap::new(),
            log_queue: VecDeque::new(),
        }
    }
}

impl SyslogReceiverMut for TrichechusState {
    fn receive(&mut self, data: String) {
        self.log_queue.push_back(data);
    }
}

#[derive(Clone)]
struct TrichechusServerImpl {
    state: Rc<RefCell<TrichechusState>>,
    transport_type: TransportType,
}

impl TrichechusServerImpl {
    fn new(state: Rc<RefCell<TrichechusState>>, transport_type: TransportType) -> Self {
        TrichechusServerImpl {
            state,
            transport_type,
        }
    }

    fn port_to_transport_type(&self, port: u32) -> TransportType {
        let mut result = self.transport_type.clone();
        match &mut result {
            TransportType::IpConnection(addr) => addr.set_port(port as u16),
            TransportType::VsockConnection(addr) => {
                addr.port = port;
            }
            _ => panic!("unexpected connection type"),
        }
        result
    }
}

impl Trichechus for TrichechusServerImpl {
    type Error = ();

    fn start_session(&self, app_info: AppInfo) -> StdResult<(), ()> {
        info!(
            "Received start session message with app_id: {}",
            app_info.app_id
        );
        // The TEE app isn't started until its socket connection is accepted.
        self.state.borrow_mut().pending_apps.insert(
            self.port_to_transport_type(app_info.port_number),
            app_info.app_id,
        );
        Ok(())
    }

    fn get_logs(&self) -> StdResult<Vec<String>, ()> {
        let mut replacement: VecDeque<String> = VecDeque::new();
        swap(&mut self.state.borrow_mut().log_queue, &mut replacement);
        Ok(replacement.into())
    }
}

struct DugongConnectionHandler {
    state: Rc<RefCell<TrichechusState>>,
}

impl DugongConnectionHandler {
    fn new(state: Rc<RefCell<TrichechusState>>) -> Self {
        DugongConnectionHandler { state }
    }

    fn connect_tee_app(&mut self, app_id: &str, connection: Transport) {
        let id = connection.id.clone();
        match spawn_tee_app(app_id, connection) {
            Ok(s) => {
                self.state.borrow_mut().running_apps.insert(id, s).unwrap();
            }
            Err(e) => {
                error!("failed to start tee app: {}", e);
            }
        }
    }
}

impl ConnectionHandler for DugongConnectionHandler {
    fn handle_incoming_connection(&mut self, connection: Transport) -> Option<Box<dyn Mutator>> {
        let expected_port = self.state.borrow().expected_port;
        // Check if the incoming connection is expected and associated with a TEE
        // application.
        let reservation = self.state.borrow_mut().pending_apps.remove(&connection.id);
        if let Some(app_id) = reservation {
            self.connect_tee_app(&app_id, connection);
            // TODO return a AddEventSourceMutator that cleans up the sandboxed app when
            // dropped.
            None
        } else {
            // Check if it is a control connection.
            match connection.id.get_port() {
                Ok(port) if port == expected_port => Some(Box::new(AddEventSourceMutator(Some(
                    Box::new(RpcDispatcher::new(
                        TrichechusServerImpl::new(self.state.clone(), connection.id.clone())
                            .box_clone(),
                        connection,
                    )),
                )))),
                _ => None,
            }
        }
    }
}

fn get_app_path(id: &str) -> Result<&str> {
    match id {
        "shell" => Ok("/bin/sh"),
        id => Err(Error::InvalidAppId(id.to_string())),
    }
}

fn spawn_tee_app(app_id: &str, transport: Transport) -> Result<TEEApp> {
    let mut sandbox = Sandbox::new(None).map_err(Error::NewSandbox)?;
    let (trichechus_transport, tee_transport) =
        create_transport_from_pipes().map_err(Error::NewTransport)?;
    let keep_fds: [(RawFd, RawFd); 5] = [
        (transport.r.as_raw_fd(), CROS_CONNECTION_R_FD),
        (transport.w.as_raw_fd(), CROS_CONNECTION_W_FD),
        (transport.w.as_raw_fd(), CROS_CONNECTION_ERR_FD),
        (tee_transport.r.as_raw_fd(), DEFAULT_CONNECTION_R_FD),
        (tee_transport.w.as_raw_fd(), DEFAULT_CONNECTION_W_FD),
    ];
    let process_path = get_app_path(app_id)?;

    sandbox
        .run(Path::new(process_path), &[process_path], &keep_fds)
        .map_err(Error::RunSandbox)?;

    Ok(TEEApp {
        sandbox,
        transport: trichechus_transport,
    })
}

// TODO: Figure out how to clean up TEEs that are no longer in use
// TODO: Figure out rate limiting and prevention against DOS attacks
// TODO: What happens if dugong crashes? How do we want to handle
fn main() -> Result<()> {
    // Handle the arguments first since "-h" shouldn't have any side effects on the system such as
    // creating /dev/log.
    let args: Vec<String> = env::args().collect();
    let mut opts = Options::new();
    opts.optopt(
        SYSLOG_PATH_SHORT_NAME,
        "syslog-path",
        "connect to trichechus, get and print logs, then exit.",
        SYSLOG_PATH,
    );
    let (config, matches) = initialize_common_arguments(opts, &args[1..]).unwrap();
    let state = Rc::new(RefCell::new(TrichechusState::new()));

    // Create /dev/log if it doesn't already exist since trichechus is the first thing to run after
    // the kernel on the hypervisor.
    let log_path = PathBuf::from(
        matches
            .opt_str(SYSLOG_PATH_SHORT_NAME)
            .unwrap_or(SYSLOG_PATH.to_string()),
    );
    let syslog: Option<Syslog> = if !log_path.exists() {
        eprintln!("Creating syslog.");
        Some(Syslog::new(log_path, state.clone()).unwrap())
    } else {
        eprintln!("Syslog exists.");
        None
    };

    // Before logging is initialized eprintln(...) and println(...) should be used. Afterward,
    // info!(...), and error!(...) should be used instead.
    if let Err(e) = syslog::init() {
        eprintln!("Failed to initialize syslog: {}", e);
        return Err(Error::InitSyslog(e));
    }
    info!("starting trichechus: {}", BUILD_TIMESTAMP);

    if getpid() != getsid(None).unwrap() {
        // This is safe because we expect to be our own process group leader.
        if let Err(err) = unsafe { setsid() } {
            error!("Unable to start new process group: {}", err);
        }
    }
    to_sys_util::block_all_signals();
    // This is safe because no additional file descriptors have been opened (except syslog which
    // cannot be dropped until we are ready to clean up /dev/log).
    let ret = unsafe { to_sys_util::fork() }.unwrap();
    if ret != 0 {
        // The parent process collects the return codes from the child processes, so they do not
        // remain zombies.
        while to_sys_util::wait_for_child() {}
        info!("reaper done!");
        return Ok(());
    }

    // Unblock signals for the process that spawns the children. It might make sense to fork
    // again here for each child to avoid them blocking each other.
    to_sys_util::unblock_all_signals();

    let mut ctx = EventMultiplexer::new().unwrap();
    if let Some(event_source) = syslog {
        ctx.add_event(Box::new(event_source)).unwrap();
    }

    let server = TransportServer::new(
        &config.connection_type,
        DugongConnectionHandler::new(state.clone()),
    )
    .unwrap();
    let listen_addr = server.bound_to();
    ctx.add_event(Box::new(server)).unwrap();

    // Handle parent dugong connection.
    if let Ok(addr) = listen_addr {
        // Adjust the expected port when binding to an ephemeral port to facilitate testing.
        match addr.get_port() {
            Ok(DEFAULT_SERVER_PORT) | Err(_) => {}
            Ok(port) => {
                state.borrow_mut().expected_port = port + 1;
            }
        }
        info!("waiting for connection at: {}", addr);
    } else {
        info!("waiting for connection");
    }
    while !ctx.is_empty() {
        if let Err(e) = ctx.run_once() {
            error!("{}", e);
        };
    }

    Ok(())
}
