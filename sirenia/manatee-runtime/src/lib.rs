// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! API endpoint library for the TEE apps to communicate with Trichechus.

use std::collections::HashMap;
use std::fmt::{self, Debug, Display};

use libsirenia::communication::{read_message, write_message};
use libsirenia::transport::Transport;
use serde::de::DeserializeOwned;
use serde::{Deserialize, Serialize};

#[derive(Debug)]
pub enum Error {
    /// Functionality unimplemented.
    Unimplemented(String),
}

impl Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        use self::Error::*;

        match self {
            Unimplemented(e) => write!(f, "Function is unimplemented: {}", e),
        }
    }
}

/// The result of an operation in this crate.
pub type Result<T> = std::result::Result<T, Error>;

pub trait Storable: Copy + Serialize + DeserializeOwned {}
impl<S: Copy + Serialize + DeserializeOwned> Storable for S {}

pub fn read_raw<S: Storable>(id: &str) -> Result<S> {
    Err(Error::Unimplemented("read_raw".to_string()))
}

pub fn write_raw<S: Storable>(id: &str, data: S) -> Result<S> {
    Err(Error::Unimplemented("write_raw".to_string()))
}

/// Represents some scoped data temporarily loaded from the backing store.
pub struct ScopedData<S: Storable> {
    identifier: String,
    data: S,
}

impl<S: Storable> AsRef<S> for ScopedData<S> {
    fn as_ref(&self) -> &S {
        self.borrow()
    }
}

impl<S: Storable> AsMut<S> for ScopedData<S> {
    fn as_mut(&mut self) -> &mut S {
        self.borrow_mut()
    }
}

impl<S: Storable> ScopedData<S> {
    /// Reads the data into itself then writes back on a flush.
    fn new(identifier: &str) -> Result<Self> {
        let data = read_raw(identifier).unwrap();
        let id = identifier.to_string();
        Ok(ScopedData {
            identifier: id,
            data,
        })
    }

    /// Borrows the data read-only.
    fn borrow(&self) -> &S {
        &self.data
    }

    /// Borrows a mutable reference to the data (the ScopedData must be
    /// constructed read-write).
    fn borrow_mut(&mut self) -> &mut S {
        &mut self.data
    }

    /// Write the data back out to the backing store.
    fn flush(&mut self) -> Result<()> {
        write_raw(&self.identifier, self.data);
        Ok(())
    }

    /// Drop the changes without committing them.
    fn abandon(self) -> Result<()> {
        Ok(())
    }
}

/// Represents an entire key value store for one identifier.
pub struct ScopedKeyValueStore<S: Storable> {
    identifier: String,
    map: HashMap<String, S>,
}

impl<S: Storable> ScopedKeyValueStore<S> {
    /// Creates a new key value store and loads value from the backing store if
    /// it already exists.
    fn new(identifier: &str) -> Result<Self> {
        let map = HashMap::new();
        let id = identifier.to_string();
        Ok(ScopedKeyValueStore {
            identifier: id,
            map,
        })
    }

    /// Retrieves a value.
    fn get(&self, key: String) -> Result<Option<S>> {
        Err(Error::Unimplemented("get".to_string()))
    }

    /// Sets a value in the hashmap.
    fn set(&self, key: String, value: &S) -> Result<Option<S>> {
        Err(Error::Unimplemented("set".to_string()))
    }

    /// Commit changes to the backing store.
    fn flush(&mut self) -> Result<()> {
        Err(Error::Unimplemented("flush".to_string()))
    }

    /// Drop changes without committing them.
    fn abandon(&self) -> Result<()> {
        Err(Error::Unimplemented("abandon".to_string()))
    }
}
