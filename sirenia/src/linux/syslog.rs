// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A syslog server interface for use with EventMultiplexer.

use std::boxed::Box;
use std::fs::remove_file;
use std::io::{Error as IoError, Read};
use std::os::unix::io::{AsRawFd, RawFd};
use std::os::unix::net::{UnixListener, UnixStream};
use std::path::Path;

use sys_util::{self, error, handle_eintr};

use super::events::{AddEventSourceMutator, EventSource, Mutator, RemoveFdMutator};

pub const SYSLOG_PATH: &str = "/dev/log";

/// Encapsulates a connection with a a syslog client.
struct SyslogClient(UnixStream);

impl AsRawFd for SyslogClient {
    fn as_raw_fd(&self) -> RawFd {
        self.0.as_raw_fd()
    }
}

/// This currently just makes sure the read buffer doesn't fill up, but will eventually need to be
/// written out to have a handler for the data is that is read from the stream.
impl EventSource for SyslogClient {
    fn on_event(&mut self) -> Result<Option<Box<dyn Mutator>>, String> {
        let mut buffer = [0; 1024];
        Ok(if handle_eintr!(self.0.read(&mut buffer)).is_err() {
            Some(Box::new(RemoveFdMutator(self.0.as_raw_fd())))
        } else {
            None
        })
    }
}

/// Encapsulates a unix socket listener for a syslog server that accepts client connections.
pub struct Syslog(UnixListener);

impl Syslog {
    /// Return true if there is already a syslog socket open at SYSLOG_PATH.
    pub fn is_syslog_present() -> bool {
        Path::new(SYSLOG_PATH).exists()
    }

    /// Binds a new unix socket listener at the SYSLOG_PATH.
    pub fn new() -> Result<Self, IoError> {
        Ok(Syslog(UnixListener::bind(Path::new(SYSLOG_PATH))?))
    }
}

/// Cleanup the unix socket by removing SYSLOG_PATH whenever the Syslog is dropped.
impl Drop for Syslog {
    fn drop(&mut self) {
        if let Err(e) = remove_file(SYSLOG_PATH) {
            if e.kind() != std::io::ErrorKind::NotFound {
                eprintln!("Failed to cleanup syslog: {:?}", e);
            }
        }
    }
}

impl AsRawFd for Syslog {
    fn as_raw_fd(&self) -> RawFd {
        self.0.as_raw_fd()
    }
}

/// Creates a EventSource that adds any accept connections and returns a Mutator that will add the
/// client connection to the EventMultiplexer when applied.
impl EventSource for Syslog {
    fn on_event(&mut self) -> Result<Option<Box<dyn Mutator>>, String> {
        Ok(Some(match handle_eintr!(self.0.accept()) {
            Ok((instance, _)) => Box::new(AddEventSourceMutator(Some(Box::new(SyslogClient(
                instance,
            ))))),
            Err(e) => {
                error!("syslog socket error: {:?}", e);
                Box::new(RemoveFdMutator(self.0.as_raw_fd()))
            }
        }))
    }
}
