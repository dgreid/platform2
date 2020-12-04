// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Defines the messages and abstracts out communication for storage.

use std::any::Any;
use std::fmt::{self, Debug, Display};

use serde::de::DeserializeOwned;
use serde::Serialize;

#[derive(Debug)]
pub enum Error {
    /// Id does not exist in the store
    IdNotFound(String),
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
