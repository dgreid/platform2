// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The module that handles the communication api for sending messages between
//! Dugong and Trichechus

use std::fmt::{self, Debug, Display};
use std::io::{self, BufReader, BufWriter, Read, Write};

use flexbuffers::FlexbufferSerializer;
use serde::de::DeserializeOwned;
use serde::{Deserialize, Serialize};
use sys_util::info;

const LENGTH_BYTE_SIZE: usize = 4;

#[derive(Debug)]
pub enum Error {
    /// Error on reading the message.
    Read(io::Error),
    /// Length of the message is 0, which means there was an error.
    EmptyRead,
    /// Error writing the message.
    Write(io::Error),
    /// Invalid app id.
    InvalidAppId(String),
    /// Error getting the root of a flexbuffer buf.
    GetRoot(flexbuffers::ReaderError),
    /// Error deserializing the given root.
    Deserialize(flexbuffers::DeserializationError),
    /// Error serializing a value.
    Serialize(flexbuffers::SerializationError),
}

impl Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        use self::Error::*;

        match self {
            Read(e) => write!(f, "failed to read: {}", e),
            EmptyRead => write!(f, "no data to read from socket"),
            Write(e) => write!(f, "failed to write: {}", e),
            InvalidAppId(s) => write!(f, "Invalid app id: {}", s),
            GetRoot(e) => write!(f, "Problem getting the root of flexbuffer buf: {}", e),
            Deserialize(e) => write!(f, "Error deserializing: {}", e),
            Serialize(e) => write!(f, "Error serializing: {}", e),
        }
    }
}

/// The result of an operation in this crate.
pub type Result<T> = std::result::Result<T, Error>;

#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
pub enum Request {
    StartSession(AppInfo), // TODO: Add source port
    EndSession(String),
}

// TODO: Eventually we will most likely want this to accept the same
// parameters from the log function of the Syslog trait
#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
pub enum Response {
    StartConnection,
    LogInfo(String),
    LogError(String),
}

#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
pub struct AppInfo {
    pub app_id: String,
    pub port_number: u16,
}

pub fn get_app_path(id: &str) -> Result<&str> {
    match id {
        "shell" => Ok("/bin/sh"),
        id => Err(Error::InvalidAppId(id.to_string())),
    }
}

// TODO: Eventually we will want a timeout
// Reads a message from the given Read. First reads a u32 that says the length
// of the serialized message, then reads the serialized message and
// deserializes it.
pub fn read_message<R: Read, D: DeserializeOwned>(r: &mut R) -> Result<D> {
    info!("Reading message");
    let mut reader = BufReader::new(r);

    // Read the length of the serialized message first
    let mut buf = [0; LENGTH_BYTE_SIZE];
    reader.read_exact(&mut buf).map_err(Error::Read)?;

    let message_size: u32 = u32::from_be_bytes(buf);

    if message_size == 0 {
        return Err(Error::EmptyRead);
    }

    // Read the actual serialized message
    let mut ser_message = vec![0; message_size as usize];
    reader.read_exact(&mut ser_message).map_err(Error::Read)?;
    let ser_reader = flexbuffers::Reader::get_root(&ser_message).map_err(Error::GetRoot)?;

    Ok(D::deserialize(ser_reader).map_err(Error::Deserialize)?)
}

// Writes the given message to the given Write. First writes the length of the
// serialized message then the serialized message itself.
pub fn write_message<W: Write, S: Serialize + Debug>(w: &mut W, m: S) -> Result<()> {
    let mut writer = BufWriter::new(w);

    // Serialize the message and calculate the length
    let mut ser = FlexbufferSerializer::new();
    m.serialize(&mut ser).map_err(Error::Serialize)?;

    let len: u32 = ser.view().len() as u32;

    let mut len_ser = FlexbufferSerializer::new();
    len.serialize(&mut len_ser).map_err(Error::Serialize)?;

    writer.write(&len.to_be_bytes()).map_err(Error::Write)?;
    writer.write(ser.view()).map_err(Error::Write)?;

    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    use std::fs::File;
    use sys_util::pipe;

    fn open_connection() -> (File, File) {
        return pipe(false).unwrap();
    }

    #[test]
    fn get_sh_app_path() {
        assert_eq!(get_app_path(&"shell").unwrap(), "/bin/sh");
    }

    #[test]
    fn get_default_app_path() {
        assert!(get_app_path(&"foo").is_err());
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
        let message = Response::StartConnection;

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
