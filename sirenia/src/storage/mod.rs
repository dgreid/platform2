// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Defines the messages and abstracts out communication for storage between
//! TEE apps, Trichechus, and Dugong.

use std::sync::{Arc, Once};

use sync::Mutex;

use libsirenia::storage::{Error, Result, Storage};
use libsirenia::transport::{create_transport_from_default_fds, Transport};

pub struct TrichechusStorage {
    transport: Arc<Mutex<Transport>>,
}

impl TrichechusStorage {
    /*
     * Initialize the Transport between TEE App and Trichechus.
     *
     * Note: This can only be called once as it will create a file from the
     * connection file descriptor which is unsafe if done more than once. Every
     * call made after the first will simply return the storage object.
     */
    fn new() -> Self {
        static INIT: Once = Once::new();
        static mut TRANSPORT: Option<Arc<Mutex<Transport>>> = None;
        // Safe because it is protected by Once
        INIT.call_once(|| unsafe {
            let transport = Some(Arc::new(Mutex::new(
                create_transport_from_default_fds().unwrap(),
            )));
            TRANSPORT = transport;
        });

        // Safe because TRANSPORT is only written inside the Once
        unsafe {
            TrichechusStorage {
                transport: TRANSPORT.as_ref().unwrap().clone(),
            }
        }
    }
}

impl Storage for TrichechusStorage {
    /// Read without deserializing.
    fn read_raw(&mut self, id: &str) -> Result<Vec<u8>> {
        Err(Error::WriteData(None))
    }

    /// Write without serializing.
    fn write_raw(&mut self, id: &str, data: &[u8]) -> Result<()> {
        Err(Error::WriteData(None))
    }
}
