// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Provides helper functions used by handler implementations of crosh commands.

use std::env;
use std::error;
use std::fmt::{self, Display};
use std::fs::read_to_string;
use std::io::Read;
use std::mem;
use std::process::{Command, Stdio};
use std::ptr::null_mut;
use std::sync::atomic::{AtomicBool, Ordering};

use dbus::{BusType, Connection, Message};
use libc::{c_int, sigaction, SA_RESTART, SIG_DFL};
use regex::Regex;
use sys_util::error;
use system_api::OrgChromiumSessionManagerInterface;

// 25 seconds is the default timeout for dbus-send.
pub const TIMEOUT_MILLIS: i32 = 25000;

const CROS_USER_ID_HASH: &str = "CROS_USER_ID_HASH";

static INCLUDE_DEV: AtomicBool = AtomicBool::new(false);
static INCLUDE_USB: AtomicBool = AtomicBool::new(false);

pub enum Error {
    NoMatchFound,
    WrappedError(String),
}

impl Display for Error {
    #[remain::check]
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        use self::Error::*;

        #[sorted]
        match self {
            NoMatchFound => write!(f, "No match found."),
            WrappedError(err) => write!(f, "{}", err),
        }
    }
}

impl<T: error::Error> From<T> for Error {
    fn from(err: T) -> Self {
        Error::WrappedError(format!("{:?}", err).to_string())
    }
}

// Return the user ID hash from the environment. If it is not available, fetch it from session
// manager and set the environment variable.
pub fn get_user_id_hash() -> Result<String, ()> {
    if let Ok(lookup) = env::var(CROS_USER_ID_HASH) {
        return Ok(lookup);
    }

    let connection = Connection::get_private(BusType::System).map_err(|err| {
        error!("ERROR: Failed to get D-Bus connection: {}", err);
    })?;
    let conn_path = connection.with_path(
        "org.chromium.SessionManager",
        "/org/chromium/SessionManager",
        TIMEOUT_MILLIS,
    );

    let (_, user_id_hash) = conn_path.retrieve_primary_session().map_err(|err| {
        println!("ERROR: Got unexpected result: {}", err);
    })?;

    env::set_var(CROS_USER_ID_HASH, &user_id_hash);
    Ok(user_id_hash)
}

pub fn is_chrome_feature_enabled(method_name: &str) -> Result<bool, ()> {
    let method: Message = Message::new_method_call(
        "org.chromium.ChromeFeaturesService",
        "/org/chromium/ChromeFeaturesService",
        "org.chromium.ChromeFeaturesServiceInterface",
        method_name,
    )
    .unwrap()
    .append1(get_user_id_hash()?);

    let connection = Connection::get_private(BusType::System).or_else(|err| {
        error!("ERROR: Failed to get D-Bus connection: {}", err);
        Err(())
    })?;

    let reply = connection
        .send_with_reply_and_block(method, TIMEOUT_MILLIS)
        .or_else(|err| {
            error!("ERROR: D-Bus method call failed: {}", err);
            Err(())
        })?;

    reply.get1::<bool>().ok_or_else(|| {
        error!("ERROR: Got unexpected result: {:?}", &reply);
    })
}

pub fn is_dev_mode() -> Result<bool, Error> {
    let output = Command::new("crossystem").arg("cros_debug?1").output()?;
    Ok(output.status.success())
}

pub fn is_removable() -> Result<bool, Error> {
    let dev = root_dev()?;
    let groups = Regex::new(r#"/dev/([^/]+?)p?[0-9]+$"#)?
        .captures(&dev)
        .ok_or(Error::NoMatchFound)?;

    let dev = groups.get(1).unwrap().as_str();

    match read_to_string(format!("/sys/block/{}/removable", dev)) {
        Ok(contents) => Ok(contents.trim() == "1"),
        Err(err) => Err(err.into()),
    }
}

pub fn set_dev_commands_included(value: bool) {
    INCLUDE_DEV.store(value, Ordering::Release);
}

pub fn set_usb_commands_included(value: bool) {
    INCLUDE_USB.store(value, Ordering::Release);
}

pub fn dev_commands_included() -> bool {
    INCLUDE_DEV.load(Ordering::Acquire)
}

pub fn usb_commands_included() -> bool {
    INCLUDE_USB.load(Ordering::Acquire)
}

pub fn set_signal_handlers(signums: &[c_int], handler: unsafe extern "C" fn()) {
    for &signum in signums {
        unsafe {
            let mut sigact: sigaction = mem::zeroed();
            sigact.sa_flags = SA_RESTART;
            sigact.sa_sigaction = handler as *const () as usize;

            let ret = sigaction(signum, &sigact, null_mut());
            if ret < 0 {
                error!("sigaction failed for {}", signum);
            }
        }
    }
}

pub fn clear_signal_handlers(signums: &[c_int]) {
    for &signum in signums {
        unsafe {
            let mut sigact: sigaction = mem::zeroed();
            sigact.sa_sigaction = SIG_DFL;

            let ret = sigaction(signum, &sigact, null_mut());
            if ret < 0 {
                error!("sigaction failed for {}", signum);
            }
        }
    }
}

fn root_dev() -> Result<String, Error> {
    let mut child = Command::new("rootdev")
        .arg("-s")
        .stdin(Stdio::null())
        .stdout(Stdio::piped())
        .spawn()?;

    let mut result = String::new();
    child.stdout.take().unwrap().read_to_string(&mut result)?;
    child.wait()?;

    Ok(result.trim().to_string())
}
