// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Defines the messages and abstracts out communication for storage between
//! TEE apps, Trichechus, and Dugong.

use libsirenia::communication::storage::{self, Error, Result, Storable, Storage};
use libsirenia::transport::Transport;

pub struct TrichechusStorage {
    // TODO: This will have to store some state to communicate with Trichechus?
    connection: Transport,
}

impl Storage for TrichechusStorage {
    fn new() -> Self {
        panic!()
    }

    fn read_data<S: Storable>(&mut self, id: &str) -> Result<S> {
        Err(Error::ReadData)
    }

    fn write_data<S: Storable>(&mut self, id: &str, data: &S) -> Result<()> {
        Err(Error::WriteData)
    }
}
