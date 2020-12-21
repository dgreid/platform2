// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implements Chrome OS specific logic such as code that depends on system_api.

use std::io::Error as IoError;
use std::path::{Path, PathBuf};
use std::process::Command;
use std::time::Duration;

use dbus::blocking::Connection;
use dbus::Error as DbusError;
use system_api::client::OrgChromiumSessionManagerInterface;
use thiserror::Error as ThisError;

// 25 seconds is the default timeout for dbus-send.
pub const DBUS_TIMEOUT: Duration = Duration::from_secs(25);
const DAEMONSTORE_BASE_PATH: &str = "/run/daemon-store/";

#[derive(ThisError, Debug)]
pub enum Error {
    #[error("D-Bus failed to connect: {0:?}")]
    DbusConnection(DbusError),
    #[error("D-Bus call failed: {0:?}")]
    DbusMethodCall(DbusError),
    #[error("failed to get command output: {0:?}")]
    CommandOutput(IoError),
}

pub type Result<R> = std::result::Result<R, Error>;

/// Fetch the user ID hash from session manager as a hexadecimal string.
pub fn get_user_id_hash() -> Result<String> {
    let connection = Connection::new_system().map_err(Error::DbusConnection)?;
    let conn_path = connection.with_proxy(
        "org.chromium.SessionManager",
        "/org/chromium/SessionManager",
        DBUS_TIMEOUT,
    );

    let (_, user_id_hash) = conn_path
        .retrieve_primary_session()
        .map_err(Error::DbusMethodCall)?;

    Ok(user_id_hash)
}

/// Return the expected daemonstore path of the specified daemon if there is an active user session.
pub fn get_daemonstore_path(daemon_name: &str) -> Result<PathBuf> {
    let user_hash = get_user_id_hash()?;
    Ok(Path::new(DAEMONSTORE_BASE_PATH)
        .join(daemon_name)
        .join(user_hash))
}

/// Return true if the device is in developer mode.
pub fn is_dev_mode() -> Result<bool> {
    // TODO(https://crbug.com/1163531) Replace with bindings to the vboot reference library.
    let output = Command::new("crossystem")
        .arg("cros_debug?1")
        .output()
        .map_err(Error::CommandOutput)?;
    Ok(output.status.success())
}
