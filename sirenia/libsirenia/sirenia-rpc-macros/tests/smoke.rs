// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Verify sirenia-rpc_macros works for the intended use case.

extern crate sirenia_rpc_macros;

use std::thread::spawn;

use libsirenia::linux::events::EventSource;
use libsirenia::rpc::RpcDispatcher;
use libsirenia::transport::create_transport_from_pipes;
use sirenia_rpc_macros::sirenia_rpc;

#[sirenia_rpc]
pub trait TestRpc {
    type Error;

    fn checked_neg(&self, input: i32) -> Result<Option<i32>, Self::Error>;
    fn checked_add(&self, addend_a: i32, addend_b: i32) -> Result<Option<i32>, Self::Error>;
    fn terminate(&self) -> Result<(), Self::Error>;
}

#[derive(Clone)]
struct TestRpcServerImpl {}

impl TestRpc for TestRpcServerImpl {
    type Error = ();

    fn checked_neg(&self, input: i32) -> Result<Option<i32>, Self::Error> {
        Ok(input.checked_neg())
    }

    fn checked_add(&self, addend_a: i32, addend_b: i32) -> Result<Option<i32>, Self::Error> {
        Ok(addend_a.checked_add(addend_b))
    }

    fn terminate(&self) -> Result<(), Self::Error> {
        Err(())
    }
}

#[test]
fn smoke_test() {
    let (server_transport, client_transport) = create_transport_from_pipes().unwrap();

    let handler: Box<dyn TestRpcServer> = Box::new(TestRpcServerImpl {});
    let mut dispatcher = RpcDispatcher::new(handler, server_transport);

    // Queue the client RPC:
    let client_thread = spawn(move || {
        let rpc_client = TestRpcClient::new(client_transport);

        let neg_resp = rpc_client.checked_neg(125).unwrap();
        assert!(matches!(neg_resp, Some(-125)));

        let add_resp = rpc_client.checked_add(5, 4).unwrap();
        assert!(matches!(add_resp, Some(9)));

        assert!(rpc_client.terminate().is_err());
    });

    assert!(matches!(dispatcher.on_event(), Ok(None)));
    assert!(matches!(dispatcher.on_event(), Ok(None)));
    assert!(matches!(dispatcher.on_event(), Ok(Some(_))));
    // Explicitly call drop to close the pipe so the client thread gets the hang up since the return
    // value should be a RemoveFd mutator.
    drop(dispatcher);

    client_thread.join().unwrap();
}
