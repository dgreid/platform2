// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The module that handles the communication api for sending messages between
//! Dugong and Trichechus

use std::fmt::Debug;

use serde::{Deserialize, Serialize};

pub mod storage;

#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
pub enum Request {
    StartSession(AppInfo), // TODO: Add source port
    EndSession(String),
}

// TODO: Eventually we will most likely want this to accept the same
// parameters from the log function of the Syslog trait
#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
pub enum Response {
    LogInfo(String),
    LogError(String),
}

#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
pub struct AppInfo {
    pub app_id: String,
    pub port_number: u32,
}

#[cfg(test)]
mod tests {
    use super::*;

    use std::fs::File;
    use std::io::Write;

    use libsirenia::communication::{read_message, write_message, Error, LENGTH_BYTE_SIZE};
    use sys_util::pipe;

    fn open_connection() -> (File, File) {
        return pipe(false).unwrap();
    }

    #[test]
    fn send_and_recv_request() {
        let (mut r, mut w) = open_connection();
        let message = Request::StartSession(AppInfo {
            app_id: "foo".to_string(),
            port_number: 12,
        });

        write_message(&mut w, message.clone()).unwrap();

        assert_eq!(message, read_message(&mut r).unwrap());
    }

    #[test]
    fn send_and_recv_response() {
        let (mut r, mut w) = open_connection();
        let message = Response::LogInfo("info".to_string());

        write_message(&mut w, message.clone()).unwrap();

        assert_eq!(message, read_message(&mut r).unwrap());
    }

    #[test]
    fn read_error() {
        let (mut r, mut w) = open_connection();
        let buf: [u8; 1] = [2];

        w.write(&buf).unwrap();
        drop(w);

        match read_message::<File, Response>(&mut r) {
            Err(Error::Read(_)) => (),
            e => panic!("Got unexpected result: {:?}", e),
        }
    }

    #[test]
    fn empty_read_error() {
        let (mut r, mut w) = open_connection();
        let buf: [u8; LENGTH_BYTE_SIZE] = [0; LENGTH_BYTE_SIZE];

        w.write(&buf).unwrap();
        drop(w);

        match read_message::<File, Response>(&mut r) {
            Err(Error::EmptyRead) => (),
            e => panic!("Got unexpected result: {:?}", e),
        }
    }

    #[test]
    fn no_message_to_read_error() {
        let (mut r, mut w) = open_connection();
        let buf: [u8; LENGTH_BYTE_SIZE] = [1; LENGTH_BYTE_SIZE];

        w.write(&buf).unwrap();
        drop(w);

        match read_message::<File, Response>(&mut r) {
            Err(Error::Read(_)) => (),
            e => panic!("Got unexpected result: {:?}", e),
        }
    }

    #[test]
    fn get_root_error() {
        let (mut r, mut w) = open_connection();
        let buf1: [u8; LENGTH_BYTE_SIZE] = [0, 0, 0, 4];
        let buf2: [u8; 4] = [0, 0, 0, 0];

        w.write(&buf1).unwrap();
        w.write(&buf2).unwrap();
        drop(w);

        match read_message::<File, Response>(&mut r) {
            Err(Error::GetRoot(_)) => (),
            e => panic!("Got unexpected result: {:?}", e),
        }
    }
}
