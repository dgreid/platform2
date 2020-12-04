// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! API endpoint library for the TEE apps to communicate with Trichechus.

use std::borrow::{Borrow, BorrowMut};
use std::collections::HashMap;
use std::fmt::{self, Debug, Display};

use libsirenia::communication::storage::{self, Storable, Storage};

#[derive(Debug)]
pub enum Error {
    /// Error reading data from storage
    ReadData(storage::Error),
    /// Error writing data to storage
    WriteData(storage::Error),
}

impl Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        use self::Error::*;

        match self {
            ReadData(e) => write!(f, "Problem reading data from storage: {}", e),
            WriteData(e) => write!(f, "Problem writing data to storage: {}", e),
        }
    }
}

/// The result of an operation in this crate.
pub type Result<T> = std::result::Result<T, storage::Error>;

/// Represents some scoped data temporarily loaded from the backing store.
pub struct ScopedData<S: Storable, T: Storage> {
    identifier: String,
    data: S,
    storage: T,
}

impl<S: Storable, T: Storage> AsRef<S> for ScopedData<S, T> {
    fn as_ref(&self) -> &S {
        self.borrow()
    }
}

impl<S: Storable, T: Storage> AsMut<S> for ScopedData<S, T> {
    fn as_mut(&mut self) -> &mut S {
        self.borrow_mut()
    }
}

/// Borrows the data read-only.
impl<S: Storable, T: Storage> Borrow<S> for ScopedData<S, T> {
    fn borrow(&self) -> &S {
        &self.data
    }
}

/// Borrows a mutable reference to the data (the ScopedData must be
/// constructed read-write).
impl<S: Storable, T: Storage> BorrowMut<S> for ScopedData<S, T> {
    fn borrow_mut(&mut self) -> &mut S {
        &mut self.data
    }
}

impl<S: Storable, T: Storage> Drop for ScopedData<S, T> {
    fn drop(&mut self) {
        // TODO: Figure out how we want to handle errors on storing.
        // We might want to log failures, but not necessarily crash. This will
        // require us to bind mount the log into the sandbox though
        // (which we should probably do anyway).
        //
        // One option would be set a callback. We could provide some standard
        // callbacks like unwrap and log.
        self.storage
            .write_data(&self.identifier, &self.data)
            .unwrap();
    }
}

impl<S: Storable, T: Storage> ScopedData<S, T> {
    /// Reads the data into itself then writes back on a flush.
    pub fn new(mut storage: T, identifier: &str, f: fn(&str) -> S) -> Result<Self> {
        match storage.read_data(identifier) {
            Ok(data) => {
                let id = identifier.to_string();
                Ok(ScopedData {
                    identifier: id,
                    data,
                    storage,
                })
            }
            Err(storage::Error::IdNotFound(_)) => {
                let data = f(identifier);
                let id = identifier.to_string();
                Ok(ScopedData {
                    identifier: id,
                    data,
                    storage,
                })
            }
            Err(e) => Err(e),
        }
    }

    /// Write the data back out to the backing store.
    pub fn flush(&mut self) -> Result<()> {
        self.storage
            .write_data(&self.identifier, &self.data)
            .unwrap();
        Ok(())
    }

    /// Drop the changes without committing them.
    pub fn abandon(&mut self) -> Result<()> {
        self.data = self.storage.read_data(&self.identifier).unwrap();
        Ok(())
    }
}

/// Represents an entire key value store for one identifier.
pub type ScopedKeyValueStore<S, T> = ScopedData<HashMap<String, S>, T>;
