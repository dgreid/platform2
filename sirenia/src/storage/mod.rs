// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Defines the messages and abstracts out communication for storage between
//! TEE apps, Trichechus, and Dugong.

use libsirenia::storage::{Error, Result, Storable, Storage};
use libsirenia::transport::Transport;

pub struct TrichechusStorage {
    // TODO: This will have to store some state to communicate with Trichechus?
    connection: Transport,
}

impl TrichechusStorage {
    fn new() -> Self {
        panic!()
    }
}

impl Storage for TrichechusStorage {
    fn read_data<S: Storable>(&mut self, id: &str) -> Result<S> {
        Err(Error::ReadData(None))
    }

    fn write_data<S: Storable>(&mut self, id: &str, data: &S) -> Result<()> {
        Err(Error::WriteData(None))
    }
}
