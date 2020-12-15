// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Defines the messages and abstracts out communication for storage.

use std::any::Any;
use std::fmt::{self, Debug, Display};
use std::result::Result as StdResult;

use flexbuffers::FlexbufferSerializer;
use serde::de::{Deserialize, DeserializeOwned, Visitor};
use serde::export::Formatter;
use serde::{Deserializer, Serialize, Serializer};
use std::borrow::Borrow;

#[derive(Debug)]
pub enum Error {
    /// Id does not exist in the store
    IdNotFound(String),
    /// Error casting data
    CastData,
    /// Error reading data from storage
    ReadData,
    /// Error writing data to storage
    WriteData,
}

impl Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        use self::Error::*;

        match self {
            IdNotFound(e) => write!(f, "Id not found: {}", e),
            CastData => write!(f, "Problem casting data"),
            ReadData => write!(f, "Problem reading data from storage"),
            WriteData => write!(f, "Problem writing data to storage"),
        }
    }
}

/// The result of an operation in this crate.
pub type Result<T> = std::result::Result<T, Error>;

pub trait Storable: Any + Serialize + DeserializeOwned {}
impl<S: Any + Serialize + DeserializeOwned> Storable for S {}

pub trait Storage {
    fn new() -> Self;
    fn read_data<S: Storable>(&mut self, id: &str) -> Result<S>;
    fn write_data<S: Storable>(&mut self, id: &str, data: &S) -> Result<()>;
}

/// A flexible type that can be used in storable data structures. This should be used sparingly
/// because it results in nested serialization.
pub enum StorableMember {
    Deserialized {
        value: Box<dyn Any>,
        store: fn(&dyn Any) -> Result<Vec<u8>>,
    },
    // TODO consider zero copy alternatives to Vec.
    Serialized(Vec<u8>),
}

impl StorableMember {
    pub fn new_serialized(data: Vec<u8>) -> Self {
        StorableMember::Serialized(data)
    }

    pub fn new_deserialized<S: Storable>(value: S) -> Self {
        StorableMember::Deserialized {
            value: Box::new(value),
            store: store::<S>,
        }
    }

    pub fn interpret<S: Storable>(&mut self) -> Result<&mut Self> {
        if let StorableMember::Serialized(data) = self {
            let ser_reader = flexbuffers::Reader::get_root(&data).map_err(|_| Error::ReadData)?;
            *self = StorableMember::new_deserialized(
                S::deserialize(ser_reader).map_err(|_| Error::ReadData)?,
            );
        }
        Ok(self)
    }

    pub fn try_borrow_mut<S: Storable>(&mut self) -> Result<&mut S> {
        if let StorableMember::Deserialized { value, store: _ } = self.interpret::<S>()? {
            if let Some(value) = value.downcast_mut::<S>() {
                return Ok(value);
            }
        }
        Err(Error::CastData)
    }

    pub fn try_borrow<S: Storable>(&self) -> Result<&S> {
        if let StorableMember::Deserialized { value, store: _ } = self {
            if let Some(value) = value.downcast_ref::<S>() {
                return Ok(value);
            }
        }
        Err(Error::CastData)
    }
}

fn store<S: Storable>(val: &dyn std::any::Any) -> Result<Vec<u8>> {
    if let Some(value) = val.downcast_ref::<S>() {
        let mut ser = FlexbufferSerializer::new();
        value.serialize(&mut ser).map_err(|_| Error::WriteData)?;
        Ok(ser.take_buffer())
    } else {
        Err(Error::CastData)
    }
}

impl Into<Vec<u8>> for StorableMember {
    fn into(self) -> Vec<u8> {
        match self {
            StorableMember::Deserialized { value, store } => store(value.borrow()).unwrap(),
            StorableMember::Serialized(value) => value,
        }
    }
}

impl Serialize for StorableMember {
    fn serialize<S: Serializer>(&self, serializer: S) -> StdResult<S::Ok, S::Error> {
        let data;
        serializer.serialize_bytes(match &self {
            StorableMember::Deserialized { value, store } => {
                data = store(value.borrow()).unwrap();
                &data
            }
            StorableMember::Serialized(value) => value,
        })
    }
}

struct StorableMemberVisitor;

impl<'de> Visitor<'de> for StorableMemberVisitor {
    type Value = StorableMember;

    fn expecting(&self, formatter: &mut Formatter) -> fmt::Result {
        formatter.write_str("bytes")
    }

    fn visit_bytes<E: std::error::Error>(self, v: &[u8]) -> StdResult<Self::Value, E> {
        Ok(StorableMember::new_serialized(v.to_vec()))
    }
}

impl<'de> Deserialize<'de> for StorableMember {
    fn deserialize<D: Deserializer<'de>>(deserializer: D) -> StdResult<Self, D::Error> {
        deserializer.deserialize_bytes(StorableMemberVisitor)
    }
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn storablemember_internal_test() {
        let test_value = "Test value".to_string();

        let to_write = StorableMember::new_deserialized(test_value.clone());
        let serialized: Vec<u8> = to_write.into();
        let mut to_read = StorableMember::new_serialized(serialized);

        assert_eq!(to_read.try_borrow_mut::<String>().unwrap(), &test_value);
    }

    #[test]
    fn storablemember_external_test() {
        let test_value = "Test value".to_string();
        let to_write = StorableMember::new_deserialized(test_value.clone());

        let mut ser = FlexbufferSerializer::new();
        to_write.serialize(&mut ser).unwrap();
        let serialized: Vec<u8> = ser.take_buffer();

        let ser_reader = flexbuffers::Reader::get_root(&serialized).unwrap();
        let mut to_read = StorableMember::deserialize(ser_reader).unwrap();

        assert_eq!(to_read.try_borrow_mut::<String>().unwrap(), &test_value);
    }
}
