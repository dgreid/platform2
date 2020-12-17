// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! API endpoint library for the TEE apps to communicate with Trichechus.

use std::borrow::{Borrow, BorrowMut};
use std::collections::HashMap;
use std::fmt::{self, Debug, Display};

use libsirenia::storage::{self, Storable, Storage};

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
pub struct ScopedData<'a, S: Storable, T: Storage> {
    identifier: String,
    data: S,
    storage: &'a mut T,
}

impl<'a, S: Storable, T: Storage> AsRef<S> for ScopedData<'a, S, T> {
    fn as_ref(&self) -> &S {
        self.borrow()
    }
}

impl<'a, S: Storable, T: Storage> AsMut<S> for ScopedData<'a, S, T> {
    fn as_mut(&mut self) -> &mut S {
        self.borrow_mut()
    }
}

/// Borrows the data read-only.
impl<'a, S: Storable, T: Storage> Borrow<S> for ScopedData<'a, S, T> {
    fn borrow(&self) -> &S {
        &self.data
    }
}

/// Borrows a mutable reference to the data (the ScopedData must be
/// constructed read-write).
impl<'a, S: Storable, T: Storage> BorrowMut<S> for ScopedData<'a, S, T> {
    fn borrow_mut(&mut self) -> &mut S {
        &mut self.data
    }
}

impl<'a, S: Storable, T: Storage> Drop for ScopedData<'a, S, T> {
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

/// Reads the data into itself then writes back on a flush.
impl<'a, S: Storable, T: Storage> ScopedData<'a, S, T> {
    /// Creates and returns a new scoped data. Attempts to read the value of
    /// the id from the backing store and uses the passed in closure to
    /// determine the default value if the id is not found.
    pub fn new(storage: &'a mut T, identifier: &str, f: fn(&str) -> S) -> Result<Self> {
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
}

/// Represents an entire key value store for one identifier.
pub type ScopedKeyValueStore<'a, S, T> = ScopedData<'a, HashMap<String, S>, T>;

#[cfg(test)]
mod tests {
    use super::*;

    use std::time::{SystemTime, UNIX_EPOCH};
    use storage::StorableMember;

    const TEST_ID: &str = "id";

    struct MockStorage {
        map: HashMap<String, StorableMember>,
    }

    impl MockStorage {
        fn new() -> Self {
            let map = HashMap::new();
            MockStorage { map }
        }
    }

    impl Storage for MockStorage {
        fn read_data<S: Storable>(&mut self, id: &str) -> Result<S> {
            match self.map.get(id) {
                Some(val) => {
                    let data = val.try_borrow::<S>().unwrap();
                    Ok(data.to_owned())
                }
                None => Err(storage::Error::IdNotFound(id.to_string())),
            }
        }

        fn write_data<S: Storable>(&mut self, id: &str, data: &S) -> Result<()> {
            let store_data = StorableMember::new_deserialized::<S>(data.to_owned());
            self.map.insert(id.to_string(), store_data);
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

    fn setup_test_case(write_back: bool) -> (MockStorage, String, String) {
        let mut store = MockStorage::new();
        let id = TEST_ID;
        let s = get_test_value();
        if write_back {
            store.write_data::<String>(&id, &s).unwrap();
        }
        (store, id.to_string(), s)
    }

    fn callback_id_not_found(_s: &str) -> String {
        "Could not find id".to_string()
    }

    fn callback_id_found(_s: &str) -> String {
        unreachable!("This callback should not be called because the id was found in the store.")
    }

    #[test]
    fn write_and_read() {
        let (mut store, id, s) = setup_test_case(/* write back */ true);
        assert_eq!(s, store.read_data::<String>(&id).unwrap());
    }

    #[test]
    fn read_id_not_found() {
        let mut store = MockStorage::new();
        assert_eq!(
            storage::Error::IdNotFound(TEST_ID.to_string()).to_string(),
            store.read_data::<String>(TEST_ID).unwrap_err().to_string()
        );
    }

    #[test]
    fn make_new_scoped_data() {
        let (mut store, id, _s) = setup_test_case(/* write back */ false);
        let data: ScopedData<String, MockStorage> =
            ScopedData::<String, MockStorage>::new(&mut store, &id, callback_id_not_found).unwrap();
        let res: &String = data.borrow();
        assert_eq!("Could not find id", res);
    }

    #[test]
    fn make_existing_scoped_data() {
        let (mut store, id, s) = setup_test_case(/* write back */ true);

        let data: ScopedData<String, MockStorage> =
            ScopedData::<String, MockStorage>::new(&mut store, &id, callback_id_found).unwrap();
        let res: &String = data.borrow();
        assert_eq!(&s, res);
    }

    #[test]
    fn mut_and_drop_scoped_data() {
        let (mut store, id, mut s) = setup_test_case(/* write back */ true);

        {
            let mut data: ScopedData<String, MockStorage> =
                ScopedData::<String, MockStorage>::new(&mut store, &id, callback_id_found).unwrap();
            let res: &mut String = data.borrow_mut();
            res.push_str(" New");
        }
        s.push_str(" New");

        assert_eq!(s, store.read_data::<String>(&id).unwrap());
    }

    #[test]
    fn mut_and_flush_scoped_data() {
        let (mut store, id, mut s) = setup_test_case(/* write back */ true);

        {
            let mut data: ScopedData<String, MockStorage> =
                ScopedData::<String, MockStorage>::new(&mut store, &id, callback_id_found).unwrap();
            let res: &mut String = data.borrow_mut();
            res.push_str(" New");
            data.flush().unwrap();
        }
        s.push_str(" New");

        assert_eq!(s, store.read_data::<String>(&id).unwrap());
    }

    #[test]
    fn mut_and_drop_kvstore() {
        let mut store = MockStorage::new();
        let id = "id";
        let map = HashMap::new();
        store
            .write_data::<HashMap<String, String>>(&id, &map)
            .unwrap();

        {
            let fun = |_h: &str| panic!();
            let key = "key";
            let value = "value";
            let mut kvstore =
                ScopedKeyValueStore::<String, MockStorage>::new(&mut store, &id, fun).unwrap();
            kvstore.as_mut().insert(key.to_string(), value.to_string());
            assert!(kvstore.as_mut().contains_key(key));
        }

        let res_map = store.read_data::<HashMap<String, String>>(&id).unwrap();
        assert!(res_map.contains_key("key"));
        assert_eq!("value", res_map.get("key").unwrap())
    }
}
