// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Handles the communication abstraction for sirenia. Used both for
//! communication between dugong and trichechus and between TEEs and
//! trichechus.
//!

pub mod persistence;

use std::fmt::{self, Debug, Display};
use std::io::{self, BufWriter, Read, Write};

use flexbuffers::FlexbufferSerializer;
use serde::de::DeserializeOwned;
use serde::Serialize;
use sys_util::info;

pub const LENGTH_BYTE_SIZE: usize = 4;

#[derive(Debug)]
pub enum Error {
    /// Error on reading the message.
    Read(io::Error),
    /// Length of the message is 0, which means there was an error.
    EmptyRead,
    /// Error writing the message.
    Write(io::Error),
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
            GetRoot(e) => write!(f, "Problem getting the root of flexbuffer buf: {}", e),
            Deserialize(e) => write!(f, "Error deserializing: {}", e),
            Serialize(e) => write!(f, "Error serializing: {}", e),
        }
    }
}

/// The result of an operation in this crate.
pub type Result<T> = std::result::Result<T, Error>;

// Reads a message from the given Read. First reads a u32 that says the length
// of the serialized message, then reads the serialized message and
// deserializes it.
pub fn read_message<R: Read, D: DeserializeOwned>(r: &mut R) -> Result<D> {
    info!("Reading message");

    // Read the length of the serialized message first
    let mut buf = [0; LENGTH_BYTE_SIZE];
    r.read_exact(&mut buf).map_err(Error::Read)?;

    let message_size: u32 = u32::from_be_bytes(buf);

    if message_size == 0 {
        return Err(Error::EmptyRead);
    }

    // Read the actual serialized message
    let mut ser_message = vec![0; message_size as usize];
    r.read_exact(&mut ser_message).map_err(Error::Read)?;
    let ser_reader = flexbuffers::Reader::get_root(&ser_message).map_err(Error::GetRoot)?;

    Ok(D::deserialize(ser_reader).map_err(Error::Deserialize)?)
}

// Writes the given message to the given Write. First writes the length of the
// serialized message then the serialized message itself.
pub fn write_message<W: Write, S: Serialize>(w: &mut W, m: S) -> Result<()> {
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
