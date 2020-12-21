// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Holds all the code related to RPC over vsock.

use std::os::raw::c_uint;
use std::result::Result as StdResult;

use getopts::{Matches, Options};
use libchromeos::vsock::{SocketAddr, VsockCid};
use libsirenia::cli::{self, TransportTypeOption};
use libsirenia::communication::persistence::{Cronista, CronistaServer, Scope, Status};
use libsirenia::linux::events::EventMultiplexer;
use libsirenia::rpc::{register_server, Error as RpcError};
use libsirenia::transport::TransportType;
use sys_util::{self, error, info};
use thiserror::Error as ThisError;

use crate::storage;

#[derive(ThisError, Debug)]
pub enum Error {
    #[error("failed to parse the transport: {0:?}")]
    ParseTransport(cli::Error),
    #[error("failed to persist data: {0:?}")]
    Persist(storage::Error),
}

type Result<T> = StdResult<T, Error>;

const DEFAULT_BIND_PORT: c_uint = 5554;

/// Configuration parameters for a socket rpc instance.
pub struct Config {
    bind_addr: TransportType,
}

impl Default for Config {
    fn default() -> Self {
        Config {
            bind_addr: TransportType::VsockConnection(SocketAddr {
                cid: VsockCid::Any,
                port: DEFAULT_BIND_PORT,
            }),
        }
    }
}

/// A helper to generate a socket_rpc::Config from getopts::Options.
pub struct CliConfigGenerator {
    bind_addr: TransportTypeOption,
}

impl CliConfigGenerator {
    /// Registers the relevant parameters with the specified Options.
    pub fn new(mut opts: &mut Options) -> Self {
        CliConfigGenerator {
            bind_addr: TransportTypeOption::default(&mut opts),
        }
    }

    /// Generates a Config from the specified matches.
    pub fn generate_config(&self, matches: &Matches) -> Result<Config> {
        let mut config = Config::default();
        if let Some(cli_addr) = self
            .bind_addr
            .from_matches(&matches)
            .map_err(Error::ParseTransport)?
        {
            config.bind_addr = cli_addr;
        }
        Ok(config)
    }
}

/// Sets up a socket based RPC server on the EventMultiplexer.
pub fn register_socket_rpc(
    config: &Config,
    mut event_multiplexer: &mut EventMultiplexer,
) -> StdResult<Option<TransportType>, RpcError> {
    let handler: Box<dyn CronistaServer> = Box::new(CronistaServerImpl {});
    register_server(&mut event_multiplexer, &config.bind_addr, handler)
}

/// Manages a single RPC connection.
#[derive(Clone)]
struct CronistaServerImpl {}

impl Cronista for CronistaServerImpl {
    type Error = ();

    fn persist(
        &self,
        scope: Scope,
        domain: String,
        identifier: String,
        data: Vec<u8>,
    ) -> std::result::Result<Status, Self::Error> {
        info!("Received persist message",);
        let data: Vec<u8> = data.into();
        Ok(
            match storage::persist(scope, &domain, &identifier, data.as_slice()) {
                Ok(_) => Status::Success,
                _ => Status::Failure,
            },
        )
    }

    fn retrieve(
        &self,
        scope: Scope,
        domain: String,
        identifier: String,
    ) -> std::result::Result<(Status, Vec<u8>), Self::Error> {
        info!("Received retrieve message");
        Ok(
            match storage::retrieve(scope, &domain, &identifier).map_err(Error::Persist) {
                Ok(data) => (Status::Success, data),
                _ => (Status::Failure, Vec::new()),
            },
        )
    }
}
