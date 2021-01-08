// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Defines the messages and abstracts out communication for storage.

mod file_storage;
pub use file_storage::FileStorage;

use std::any::{type_name, Any};
use std::borrow::Borrow;
use std::error::Error as StdError;
use std::fmt::{self, Debug, Formatter};
use std::result::Result as StdResult;

use flexbuffers::{from_slice, FlexbufferSerializer};
use serde::de::{Deserialize, DeserializeOwned, Visitor};
use serde::{Deserializer, Serialize, Serializer};
use thiserror::Error as ThisError;

#[derive(ThisError, Debug)]
pub enum Error {
    #[error("id not found {0}")]
    IdNotFound(String),
    #[error("storage regjected id {0}")]
    InvalidIdForStorage(String),
    #[error("failed to cast data from '{from}' to '{to}'")]
    CastData { from: String, to: String },
    // TODO: We may want to change this to not have an argument since it is not
    // used anyways
    #[error("failed to read data")]
    ReadData(Option<Box<dyn StdError>>),
    #[error("id not written yet")]
    EmptyRead,
    #[error("failed to write data")]
    WriteData(Option<Box<dyn StdError>>),
}

pub fn to_read_data_error<E: StdError + 'static>(err: E) -> Error {
    Error::ReadData(Some(Box::new(err)))
}

pub fn to_write_data_error<E: StdError + 'static>(err: E) -> Error {
    Error::WriteData(Some(Box::new(err)))
}

/// The result of an operation in this crate.
pub type Result<T> = std::result::Result<T, Error>;

pub trait Storable: Any + Clone + Serialize + DeserializeOwned {}
impl<S: Any + Clone + Serialize + DeserializeOwned> Storable for S {}

pub trait Storage {
    fn read_raw(&mut self, id: &str) -> Result<Vec<u8>>;
    fn write_raw(&mut self, id: &str, data: &[u8]) -> Result<()>;
    fn read_data<S: Storable>(&mut self, id: &str) -> Result<S> {
        let contents = self.read_raw(id)?;
        from_slice(&contents).map_err(to_read_data_error)
    }

    fn write_data<S: Storable>(&mut self, id: &str, data: &S) -> Result<()> {
        let mut ser = FlexbufferSerializer::new();
        data.serialize(&mut ser).map_err(to_write_data_error)?;
        self.write_raw(id, &ser.take_buffer())
    }
}

/// A flexible type that can be used in storable data structures. This should be used sparingly
/// because it results in nested serialization.
pub enum StorableMember {
    Deserialized {
        value: Box<dyn Any>,
        store: fn(&dyn Any) -> Result<Vec<u8>>,
        typename: &'static str,
    },
    // TODO consider zero copy alternatives to Vec.
    Serialized(Vec<u8>),
}

const SERIALIZED: &str = "<unknown serialized>";

impl StorableMember {
    pub fn new_serialized(data: Vec<u8>) -> Self {
        StorableMember::Serialized(data)
    }

    pub fn new_deserialized<S: Storable>(value: S) -> Self {
        StorableMember::Deserialized {
            value: Box::new(value),
            store: store::<S>,
            typename: type_name::<Self>(),
        }
    }

    fn get_typename(&self) -> &'static str {
        match self {
            StorableMember::Deserialized {
                value: _,
                store: _,
                typename,
            } => typename,
            StorableMember::Serialized(_) => SERIALIZED,
        }
    }

    pub fn interpret<S: Storable>(&mut self) -> Result<&mut Self> {
        if let StorableMember::Serialized(data) = self {
            let ser_reader = flexbuffers::Reader::get_root(&data).map_err(to_read_data_error)?;
            *self = StorableMember::new_deserialized(
                S::deserialize(ser_reader).map_err(to_read_data_error)?,
            );
        }
        Ok(self)
    }

    pub fn try_borrow_mut<S: Storable>(&mut self) -> Result<&mut S> {
        self.interpret::<S>()?;
        match self {
            StorableMember::Deserialized {
                value,
                store: _,
                typename,
            } => match value.downcast_mut::<S>() {
                Some(value) => Ok(value),
                None => Err(Error::CastData {
                    from: typename.to_string(),
                    to: type_name::<S>().to_string(),
                }),
            },
            StorableMember::Serialized(_) => Err(Error::CastData {
                from: self.get_typename().to_string(),
                to: type_name::<S>().to_string(),
            }),
        }
    }

    pub fn try_borrow<S: Storable>(&self) -> Result<&S> {
        if let StorableMember::Deserialized {
            value,
            store: _,
            typename: _,
        } = self
        {
            if let Some(value) = value.downcast_ref::<S>() {
                return Ok(value);
            }
        }
        Err(Error::CastData {
            from: self.get_typename().to_string(),
            to: type_name::<S>().to_string(),
        })
    }
}

fn store<S: Storable>(val: &dyn Any) -> Result<Vec<u8>> {
    if let Some(value) = val.downcast_ref::<S>() {
        let mut ser = FlexbufferSerializer::new();
        value.serialize(&mut ser).map_err(to_write_data_error)?;
        Ok(ser.take_buffer())
    } else {
        Err(Error::CastData {
            from: type_name::<dyn Any>().to_string(),
            to: type_name::<S>().to_string(),
        })
    }
}

impl Into<Vec<u8>> for StorableMember {
    fn into(self) -> Vec<u8> {
        match self {
            StorableMember::Deserialized {
                value,
                store,
                typename: _,
            } => store(value.borrow()).unwrap(),
            StorableMember::Serialized(value) => value,
        }
    }
}

impl Serialize for StorableMember {
    fn serialize<S: Serializer>(&self, serializer: S) -> StdResult<S::Ok, S::Error> {
        let data;
        serializer.serialize_bytes(match &self {
            StorableMember::Deserialized {
                value,
                store,
                typename: _,
            } => {
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
