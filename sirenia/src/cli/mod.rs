// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Encapsulates functionality to support the command line interface with the
//! Trichechus and Dugong daemons.

use std::fmt::{self, Display};

use getopts::{self, Options};

use super::transport::{TransportType, DEFAULT_PORT, LOOPBACK_DEFAULT};
use libchromeos::vsock::{SocketAddr as VSocketAddr, VsockCid};

#[derive(Debug)]
pub enum Error {
    /// Error parsing command line options.
    CLIParse(getopts::Fail),
    TransportParse(super::transport::Error),
}

impl Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        use self::Error::*;

        match self {
            CLIParse(e) => write!(f, "failed to parse the command line options: {}", e),
            TransportParse(e) => write!(f, "failed to parse transport type: {}", e),
        }
    }
}

/// The result of an operation in this crate.
pub type Result<T> = std::result::Result<T, Error>;

/// The configuration options that can be configured by command line arguments,
/// flags, and options.
#[derive(Debug, PartialEq)]
pub struct CommonConfig {
    pub connection_type: TransportType,
}

/// Sets up command line argument parsing and generates a CommonConfig based on
/// the command line entry.
pub fn initialize_common_arguments(args: &[String]) -> Result<CommonConfig> {
    // Vsock is used as the default because it is the transport used in production.
    // IP is provided for testing and development.
    // Not sure yet what cid default makes sense or if a default makes sense at
    // all.
    let default_connection = TransportType::VsockConnection(VSocketAddr {
        cid: VsockCid::Any,
        port: DEFAULT_PORT,
    });
    let mut config = CommonConfig {
        connection_type: default_connection,
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

    if let Some(value) = matches.opt_str(url_name) {
        config.connection_type = value
            .parse::<TransportType>()
            .map_err(Error::TransportParse)?
    };
    Ok(config)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::transport::tests::{get_ip_uri, get_vsock_uri};
    use std::net::{IpAddr, Ipv4Addr, SocketAddr};

    #[test]
    fn initialize_common_arguments_invalid_args() {
        let value: [String; 1] = ["-foo".to_string()];
        let act_result = initialize_common_arguments(&value);
        match &act_result {
            Err(Error::CLIParse(_)) => (),
            _ => panic!("Got unexpected result: {:?}", &act_result),
        }
    }

    #[test]
    fn initialize_common_arguments_ip_valid() {
        let exp_socket = SocketAddr::new(IpAddr::V4(Ipv4Addr::new(1, 1, 1, 1)), 1234);
        let exp_result = CommonConfig {
            connection_type: TransportType::IpConnection(exp_socket),
        };
        let value: [String; 2] = ["-U".to_string(), get_ip_uri().to_string()];
        let act_result = initialize_common_arguments(&value).unwrap();
        assert_eq!(act_result, exp_result);
    }

    #[test]
    fn initialize_common_arguments_vsock_valid() {
        let vsock = TransportType::VsockConnection(VSocketAddr {
            cid: VsockCid::Local,
            port: 1,
        });
        let exp_result = CommonConfig {
            connection_type: vsock,
        };
        let value: [String; 2] = ["-U".to_string(), get_vsock_uri()];
        let act_result = initialize_common_arguments(&value).unwrap();
        assert_eq!(act_result, exp_result);
    }

    #[test]
    fn initialize_common_arguments_no_args() {
        let default_connection = TransportType::VsockConnection(VSocketAddr {
            cid: VsockCid::Any,
            port: DEFAULT_PORT,
        });
        let exp_result = CommonConfig {
            connection_type: default_connection,
        };
        let value: [String; 0] = [];
        let act_result = initialize_common_arguments(&value).unwrap();
        assert_eq!(act_result, exp_result);
    }
}
