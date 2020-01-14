// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Encapsulates functionality to support the command line interface with the
//! Trichechus and Dugong daemons.

use std::fmt::{self, Display};
use std::io;
use std::net::{SocketAddr, ToSocketAddrs};

use getopts::{self, Options};

use super::transport::LOOPBACK_DEFAULT;

#[derive(Debug)]
pub enum Error {
    /// Failed to parse a socket address.
    SocketAddrParse(Option<io::Error>),
    /// Got an unknown transport type.
    UnknownTransportType,
    /// Failed to parse URI.
    URIParse,
    /// Error parsing command line options.
    CLIParse(getopts::Fail),
}

impl Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        use self::Error::*;

        match self {
            SocketAddrParse(e) => match e {
                Some(i) => write!(f, "failed to parse the socket address: {}", i),
                None => write!(f, "failed to parse the socket address"),
            },
            UnknownTransportType => write!(f, "got an unrecognized transport type"),
            URIParse => write!(f, "failed to parse the URI"),
            CLIParse(e) => write!(f, "failed to parse the command line options: {}", e),
        }
    }
}

/// The result of an operation in this crate.
pub type Result<T> = std::result::Result<T, Error>;

/// Transport options that can be selected.
pub enum TransportType {
    VsockConnection,
    IpConnection(SocketAddr),
}

/// The configuration options that can be configured by command line arguments,
/// flags, and options.
pub struct CommonConfig {
    pub connection_type: TransportType,
}

fn parse_ip_connection(value: &str) -> Result<TransportType> {
    let mut iter = value
        .to_socket_addrs()
        .map_err(|e| Error::SocketAddrParse(Some(e)))?;
    match iter.next() {
        None => Err(Error::SocketAddrParse(None)),
        Some(a) => Ok(TransportType::IpConnection(a)),
    }
}

fn parse_connection_type(value: &str) -> Result<TransportType> {
    if value.is_empty() {
        return Ok(TransportType::VsockConnection);
    }
    let parts: Vec<&str> = value.split("://").collect();
    match parts.len() {
        2 => match parts[0] {
            "vsock" | "VSOCK" => Ok(TransportType::VsockConnection),
            "ip" | "IP" => parse_ip_connection(parts[1]),
            _ => Err(Error::UnknownTransportType),
        },
        1 => parse_ip_connection(value),
        _ => Err(Error::URIParse),
    }
}

/// Sets up command line argument parsing and generates a CommonConfig based on
/// the command line entry.
pub fn initialize_common_arguments(args: &[String]) -> Result<CommonConfig> {
    let mut config = CommonConfig {
        connection_type: TransportType::VsockConnection,
    };

    let url_name = "U";

    let mut opts = Options::new();
    opts.optflagopt(
        url_name,
        "server-url",
        "URL to the server",
        LOOPBACK_DEFAULT,
    );
    let matches = opts.parse(&args[..]).map_err(Error::CLIParse)?;

    config.connection_type = match matches.opt_str(url_name) {
        Some(value) => parse_connection_type(&value)?,
        None => TransportType::VsockConnection,
    };

    Ok(config)
}
