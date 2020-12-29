// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A TEE application life-cycle manager.

use std::cell::RefCell;
use std::collections::{HashMap, VecDeque};
use std::env;
use std::fmt::Debug;
use std::path::Path;
use std::rc::Rc;
use std::result::Result as StdResult;
use std::string::String;

use libsirenia::linux::events::{AddEventSourceMutator, EventMultiplexer, Mutator};
use libsirenia::linux::syslog::{Syslog, SyslogReceiverMut};
use libsirenia::rpc::{ConnectionHandler, RpcDispatcher, TransportServer};
use libsirenia::sandbox::{self, Sandbox};
use libsirenia::to_sys_util;
use libsirenia::transport::{self, Transport, TransportType, DEFAULT_CLIENT_PORT};
use sirenia::build_info::BUILD_TIMESTAMP;
use sirenia::cli::initialize_common_arguments;
use sirenia::communication::{AppInfo, Trichechus, TrichechusServer};
use sys_util::{self, error, info, syslog};
use thiserror::Error as ThisError;

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

struct TrichechusState {
    pending_apps: HashMap<TransportType, String>,
    // TODO figure out if we actually need to hold onto the running apps or not. We already reap the
    // processes, but this might be useful for killing apps that get into a bad state. As is, this
    // is never cleaned up so it has a memory leak.
    running_apps: HashMap<TransportType, Sandbox>,
    log_queue: VecDeque<String>,
}

impl TrichechusState {
    fn new() -> Self {
        TrichechusState {
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
        // Check if the incoming connection is expected and associated with a TEE
        // application.
        let reservation = self.state.borrow_mut().pending_apps.remove(&connection.id);
        match reservation {
            Some(app_id) => {
                self.connect_tee_app(&app_id, connection);
                // TODO return a AddEventSourceMutator that cleans up the sandboxed app when
                // dropped.
                None
            }
            None => {
                // Check if it is a control connection.
                if matches!(connection.id.get_port(), Ok(DEFAULT_CLIENT_PORT)) {
                    Some(Box::new(AddEventSourceMutator(Some(Box::new(
                        RpcDispatcher::new(
                            TrichechusServerImpl::new(self.state.clone(), connection.id.clone())
                                .box_clone(),
                            connection,
                        ),
                    )))))
                } else {
                    None
                }
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

fn spawn_tee_app(app_id: &str, transport: Transport) -> Result<Sandbox> {
    let mut sandbox = Sandbox::new(None).map_err(Error::NewSandbox)?;
    let process_path = get_app_path(app_id)?;

    sandbox
        .run(
            Path::new(process_path),
            &[process_path],
            transport.r.as_raw_fd(),
            transport.w.as_raw_fd(),
            transport.w.as_raw_fd(),
        )
        .map_err(Error::RunSandbox)?;

    Ok(sandbox)
}

// TODO: Figure out how to clean up TEEs that are no longer in use
// TODO: Figure out rate limiting and prevention against DOS attacks
// TODO: What happens if dugong crashes? How do we want to handle
fn main() -> Result<()> {
    // Handle the arguments first since "-h" shouldn't have any side effects on the system such as
    // creating /dev/log.
    let args: Vec<String> = env::args().collect();
    let config = initialize_common_arguments(&args[1..]).unwrap();
    let state = Rc::new(RefCell::new(TrichechusState::new()));

    // Create /dev/log if it doesn't already exist since trichechus is the first thing to run after
    // the kernel on the hypervisor.
    let syslog: Option<Syslog> = if !Syslog::is_syslog_present() {
        eprintln!("Creating syslog.");
        Some(Syslog::new(state.clone()).unwrap())
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

    ctx.add_event(Box::new(
        TransportServer::new(&config.connection_type, DugongConnectionHandler::new(state)).unwrap(),
    ))
    .unwrap();

    // Handle parent dugong connection.
    info!("waiting for connection");
    while !ctx.is_empty() {
        if let Err(e) = ctx.run_once() {
            error!("{}", e);
        };
    }

    Ok(())
}
