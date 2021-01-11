// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The broker daemon that supports Trichecus from within the Chrome OS guest machine.

use std::cell::RefCell;
use std::env;
use std::fmt::{self, Debug};
use std::rc::Rc;
use std::time::Duration;

use dbus::arg::OwnedFd;
use dbus::blocking::LocalConnection;
use dbus::tree::{self, Interface, MTFn};
use libsirenia::rpc;
use libsirenia::transport::{
    self, Transport, TransportType, DEFAULT_CLIENT_PORT, DEFAULT_SERVER_PORT,
};
use serde::export::Formatter;
use sirenia::build_info::BUILD_TIMESTAMP;
use sirenia::cli::initialize_common_arguments;
use sirenia::communication::{AppInfo, Trichechus, TrichechusClient};
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
    #[error("failed to call rpc: {0}")]
    Rpc(rpc::Error),
    #[error("failed to start up the syslog: {0}")]
    SysLog(sys_util::syslog::Error),
    #[error("failed to bind to socket: {0}")]
    TransportBind(transport::Error),
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

struct DugongDevice {
    trichechus_client: RefCell<TrichechusClient>,
    transport_type: TransportType,
}

impl Debug for DugongDevice {
    fn fmt(&self, f: &mut Formatter<'_>) -> fmt::Result {
        write!(f, "transport_type: {:?}", self.transport_type)
    }
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
    let mut transport = device.transport_type.try_into_client(None).unwrap();
    let addr = transport.bind().map_err(Error::TransportBind)?;
    let app_info = AppInfo {
        app_id: String::from(app_id),
        port_number: addr.get_port().unwrap(),
    };
    device
        .trichechus_client
        .borrow_mut()
        .start_session(app_info)
        .map_err(Error::Rpc)?;
    match transport.connect() {
        Ok(Transport { r, w, id: _ }) => unsafe {
            // This is safe because into_raw_fd transfers the ownership to OwnedFd.
            Ok((OwnedFd::new(r.into_raw_fd()), OwnedFd::new(w.into_raw_fd())))
        },
        Err(err) => Err(Error::TransportConnection(err)),
    }
}

pub fn start_dbus_handler(
    trichechus_client: TrichechusClient,
    transport_type: TransportType,
) -> Result<()> {
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
                trichechus_client: RefCell::new(trichechus_client),
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
    let transport_type = config.connection_type.clone();
    if let Err(e) = syslog::init() {
        eprintln!("failed to initialize syslog: {}", e);
        return Err(e).map_err(Error::SysLog);
    }

    info!("Starting dugong: {}", BUILD_TIMESTAMP);
    info!("Opening connection to trichechus");
    // Adjust the source port when connecting to a non-standard port to facilitate testing.
    let bind_port = match transport_type.get_port() {
        Ok(DEFAULT_SERVER_PORT) | Err(_) => DEFAULT_CLIENT_PORT,
        Ok(port) => port + 1,
    };
    let mut transport = transport_type.try_into_client(Some(bind_port)).unwrap();

    let transport = transport.connect().map_err(|e| {
        error!("transport connect failed");
        Error::TransportConnection(e)
    })?;
    info!("Starting rpc");
    let client = TrichechusClient::new(transport);

    start_dbus_handler(client, config.connection_type).unwrap();

    // TODO: If it gets here is something screwed up?
    Ok(())
}
