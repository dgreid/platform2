// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Defines the messages and abstracts out communication for storage between
//! TEE apps, Trichechus, and Dugong.

use std::sync::{Arc, Once};

use sync::Mutex;

use crate::communication::{TEEStorage, TEEStorageClient};
use libsirenia::storage::{to_read_data_error, to_write_data_error, Result, Storage};
use libsirenia::transport::{create_transport_from_default_fds, Transport};

/// Holds the rpc client for the specific instance of the TEE App.
pub struct TrichechusStorage {
    rpc: TEEStorageClient,
}

impl TrichechusStorage {
    /*
     * Initialize the Transport between TEE App and Trichechus.
     *
     * Note: This can only be called once as it will create a file from the
     * connection file descriptor which is unsafe if done more than once. Every
     * call made after the first will simply return the storage object.
     */
    pub fn new() -> Self {
        static INIT: Once = Once::new();
        static mut TRANSPORT: Option<Arc<Mutex<Transport>>> = None;
        // Safe because it is protected by Once
        INIT.call_once(|| unsafe {
            let transport = Some(Arc::new(Mutex::new(
                create_transport_from_default_fds().unwrap(),
            )));
            TRANSPORT = transport;
        });

        // TODO(allenwebb@): Make an option in the generator that allows using
        // a reference transport for the client
        // Safe because TRANSPORT is only written inside the Once
        unsafe {
            let transport: Transport = Arc::try_unwrap(TRANSPORT.as_ref().unwrap().clone())
                .unwrap()
                .into_inner();
            TrichechusStorage {
                rpc: TEEStorageClient::new(transport),
            }
        }
    }
}

impl Default for TrichechusStorage {
    fn default() -> Self {
        Self::new()
    }
}

impl From<Transport> for TrichechusStorage {
    fn from(transport: Transport) -> Self {
        TrichechusStorage {
            rpc: TEEStorageClient::new(transport),
        }
    }
}

impl Storage for TrichechusStorage {
    /// Read without deserializing.
    fn read_raw(&mut self, id: &str) -> Result<Vec<u8>> {
        // TODO: Log the rpc error.
        match self.rpc.read_data(id.to_string()) {
            Ok(res) => Ok(res),
            Err(err) => Err(to_read_data_error(err)),
        }
    }

    /// Write without serializing.
    fn write_raw(&mut self, id: &str, data: &[u8]) -> Result<()> {
        match self.rpc.write_data(id.to_string(), data.to_vec()) {
            Ok(_) => Ok(()),
            Err(err) => Err(to_write_data_error(err)),
        }
    }
}

#[cfg(test)]
pub mod tests {
    use super::*;

    use crate::communication::TEEStorageServer;
    use std::cell::RefCell;
    use std::collections::HashMap;
    use std::rc::Rc;
    use std::result::Result as StdResult;
    use std::thread::spawn;
    use std::time::{SystemTime, UNIX_EPOCH};

    use libsirenia::linux::events::EventSource;
    use libsirenia::rpc::RpcDispatcher;
    use libsirenia::storage::Error as StorageError;
    use libsirenia::transport::create_transport_from_pipes;

    const TEST_ID: &str = "id";

    #[derive(Clone)]
    struct TEEStorageServerImpl {
        map: Rc<RefCell<HashMap<String, Vec<u8>>>>,
    }

    impl TEEStorage for TEEStorageServerImpl {
        type Error = ();

        // TODO: Want to return nested Result - but Error needs to be serializable first
        fn read_data(&self, id: String) -> StdResult<Vec<u8>, Self::Error> {
            match self.map.borrow().get(&id) {
                Some(val) => Ok(val.to_vec()),
                None => Err(()),
            }
        }

        fn write_data(&self, id: String, data: Vec<u8>) -> StdResult<(), Self::Error> {
            self.map.borrow_mut().insert(id, data);
            Ok(())
        }
    }

    fn get_test_value() -> String {
        SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap()
            .as_secs()
            .to_string()
    }

    fn setup() -> (RpcDispatcher<Box<dyn TEEStorageServer>>, TrichechusStorage) {
        let (server_transport, client_transport) = create_transport_from_pipes().unwrap();

        let handler: Box<dyn TEEStorageServer> = Box::new(TEEStorageServerImpl {
            map: Rc::new(RefCell::new(HashMap::new())),
        });
        let dispatcher = RpcDispatcher::new(handler, server_transport);

        (dispatcher, TrichechusStorage::from(client_transport))
    }

    #[test]
    fn write_and_read() {
        let (mut dispatcher, mut trichechus_storage) = setup();

        let client_thread = spawn(move || {
            let data = get_test_value();
            trichechus_storage.write_data(TEST_ID, &data).unwrap();

            let retrieved_data = trichechus_storage.read_data::<String>(TEST_ID).unwrap();
            assert_eq!(retrieved_data, data);
        });

        assert!(matches!(dispatcher.on_event(), Ok(None)));
        assert!(matches!(dispatcher.on_event(), Ok(None)));

        client_thread.join().unwrap();
    }

    #[test]
    fn read_id_not_found() {
        let (mut dispatcher, mut trichechus_storage) = setup();

        let client_thread = spawn(move || {
            let error = trichechus_storage.read_data::<String>(TEST_ID).unwrap_err();
            print!("{}", error);
            assert!(matches!(error, StorageError::ReadData(_)));
        });

        assert!(matches!(dispatcher.on_event(), Ok(Some(_))));

        // Explicitly call drop to close the pipe so the client thread gets the hang up since the return
        // value should be a RemoveFd mutator.
        drop(dispatcher);

        client_thread.join().unwrap();
    }
}
