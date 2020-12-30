// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The broker daemon that supports Trichecus from within the Chrome OS guest machine.

use std::cell::RefCell;
use std::env;
use std::fmt::Debug;
use std::rc::Rc;
use std::thread::spawn;
use std::time::Duration;

use dbus::arg::OwnedFd;
use dbus::blocking::LocalConnection;
use dbus::tree::{self, Interface, MTFn};
use libchromeos::vsock::VMADDR_PORT_ANY;
use libsirenia::communication::{read_message, write_message};
use libsirenia::transport::{
    self, ClientTransport, IPClientTransport, Transport, TransportRead, TransportType,
    TransportWrite, VsockClientTransport, DEFAULT_CLIENT_PORT,
};
use sirenia::build_info::BUILD_TIMESTAMP;
use sirenia::cli::initialize_common_arguments;
use sirenia::communication::{AppInfo, Request, Response};
use sirenia::server::{org_chromium_mana_teeinterface_server, OrgChromiumManaTEEInterface};
use sys_util::{error, info, syslog};
use thiserror::Error as ThisError;

#[derive(ThisError, Debug)]
pub enum Error {
    #[error("failed to open D-Bus connection: {0}")]
    ConnectionRequest(dbus::Error),
    #[error("failed to register D-Bus handler: {0}")]
    DbusRegister(dbus::Error),
    #[error("failed to process the D-Bus message: {0}")]
    ProcessMessage(dbus::Error),
    #[error("failed to start up the syslog: {0}")]
    SysLog(sys_util::syslog::Error),
    #[error("failed to connect to socket: {0}")]
    TransportConnection(transport::Error),
}

/// The result of an operation in this crate.
pub type Result<T> = std::result::Result<T, Error>;

#[derive(Copy, Clone, Default, Debug)]
struct TData;
impl tree::DataType for TData {
    type Tree = ();
    type ObjectPath = Rc<DugongDevice>;
    type Property = ();
    type Interface = ();
    type Method = ();
    type Signal = ();
}

// TODO: May need to add more state at some point.
#[derive(Debug)]
struct DugongDevice {
    w: RefCell<Box<dyn TransportWrite>>,
    transport_type: TransportType,
}

impl OrgChromiumManaTEEInterface for DugongDevice {
    fn start_teeapplication(
        &self,
        app_id: &str,
    ) -> std::result::Result<(i32, (OwnedFd, OwnedFd)), tree::MethodErr> {
        info!("Got request to start up: {}", app_id);
        let fds = request_start_tee_app(self, app_id);
        match fds {
            Ok(fds) => Ok((0, fds)),
            Err(e) => Err(tree::MethodErr::failed(&e)),
        }
    }
}

fn request_start_tee_app(device: &DugongDevice, app_id: &str) -> Result<(OwnedFd, OwnedFd)> {
    // TODO: Need to bind to the new port to prevent other processes from using
    // it, but need to add the option to bind to an ephemeral port in vsock
    let app_info = AppInfo {
        app_id: String::from(app_id),
        port_number: 0, // TODO: Will use this later
    };
    match write_message(&mut *device.w.borrow_mut(), Request::StartSession(app_info)) {
        Ok(()) => (),
        Err(e) => error!("Error writing: {}", e),
    }
    let mut transport = open_connection(&device.transport_type, None);
    match transport.connect() {
        Ok(Transport { r, w, id: _ }) => unsafe {
            // This is safe because into_raw_fd transfers the ownership to OwnedFd.
            Ok((OwnedFd::new(r.into_raw_fd()), OwnedFd::new(w.into_raw_fd())))
        },
        Err(err) => Err(Error::TransportConnection(err)),
    }
}

pub fn start_dbus_handler(w: Box<dyn TransportWrite>, transport_type: TransportType) -> Result<()> {
    let c = LocalConnection::new_system().map_err(Error::ConnectionRequest)?;
    c.request_name(
        "org.chromium.ManaTEE",
        false, /*allow_replacement*/
        false, /*replace_existing*/
        false, /*do_not_queue*/
    )
    .map_err(Error::ConnectionRequest)?;
    let f = tree::Factory::new_fn();
    let interface: Interface<MTFn<TData>, TData> =
        org_chromium_mana_teeinterface_server(&f, (), |m| {
            let a: &Rc<DugongDevice> = m.path.get_data();
            let b: &DugongDevice = &a;
            b
        });

    let tree = f.tree(()).add(
        f.object_path(
            "/org/chromium/ManaTEE1",
            Rc::new(DugongDevice {
                w: RefCell::new(w),
                transport_type,
            }),
        )
        .introspectable()
        .add(interface),
    );

    tree.start_receive(&c);
    info!("Finished dbus setup, starting handler.");
    loop {
        c.process(Duration::from_millis(1000))
            .map_err(Error::ProcessMessage)?;
    }
}

fn main() -> Result<()> {
    let args: Vec<String> = env::args().collect();
    let config = initialize_common_arguments(&args[1..]).unwrap();
    let transport_type = config.connection_type;
    if let Err(e) = syslog::init() {
        eprintln!("failed to initialize syslog: {}", e);
        return Err(e).map_err(Error::SysLog);
    }

    info!("Starting dugong: {}", BUILD_TIMESTAMP);
    info!("Opening connection to trichechus");
    let mut transport = open_connection(&transport_type, Some(DEFAULT_CLIENT_PORT));

    if let Ok(Transport { r, w, id: _ }) = transport.connect() {
        info!("Starting rpc");
        start_rpc(transport_type, r, w);
    } else {
        error!("transport connect failed");
    }

    // TODO: If it gets here is something screwed up?
    Ok(())
}

fn open_connection(connection_type: &TransportType, port: Option<u32>) -> Box<dyn ClientTransport> {
    match connection_type {
        TransportType::IpConnection(url) => {
            Box::new(IPClientTransport::new(&url, port.unwrap_or(0) as u16).unwrap())
        }
        TransportType::VsockConnection(url) => {
            Box::new(VsockClientTransport::new(&url, port.unwrap_or(VMADDR_PORT_ANY)).unwrap())
        }
        _ => panic!("unexpected connection type"),
    }
}

fn start_rpc(transport_type: TransportType, r: Box<dyn TransportRead>, w: Box<dyn TransportWrite>) {
    info!("Opening connection to trichechus");
    // Right now just send the message to start up the shell and print and log
    // responses from trichechus
    let logger_child = spawn(move || {
        info!("starting logger");
        start_logger(r);
    });

    let dbus_child = spawn(move || {
        info!("starting dbus handler");
        start_dbus_handler(w, transport_type).unwrap();
    });
    logger_child.join().unwrap();
    dbus_child.join().unwrap();

    // TODO: What happens if these joins finish? These should be running
    // forever. How do we recover?
}
fn start_logger(mut r: Box<dyn TransportRead>) {
    loop {
        let message = read_message(&mut r).unwrap();
        match message {
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
